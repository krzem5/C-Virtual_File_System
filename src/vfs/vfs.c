#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <vfs/vfs.h>



static const vfs_flags_t _invalid_flags[]={
	[VFS_NODE_TYPE_DATA]=VFS_FLAG_SEEK_SET|VFS_FLAG_SEEK_ADD|VFS_FLAG_SEEK_END,
	[VFS_NODE_TYPE_LINK]=VFS_FLAG_READ|VFS_FLAG_WRITE|VFS_FLAG_APPEND|VFS_FLAG_SEEK_SET|VFS_FLAG_SEEK_ADD|VFS_FLAG_SEEK_END,
	[VFS_NODE_TYPE_DIRECTORY]=VFS_FLAG_READ|VFS_FLAG_WRITE|VFS_FLAG_APPEND|VFS_FLAG_SEEK_SET|VFS_FLAG_SEEK_ADD|VFS_FLAG_SEEK_END,
};



static vfs_node_t* _vfs_root_node=NULL;
static vfs_file_descriptor_t* _vfs_root_fd=NULL;
static unsigned long long int _vfs_fd_unalloc_map[VFS_MAX_FD>>6];



static void _error(const char* str){
	printf("Error: %s\n",str);
}



static vfs_node_t* _alloc_node(const char* name,unsigned short int length,vfs_node_type_t type){
	vfs_node_t* out=malloc(sizeof(vfs_node_t));
	out->name=malloc(length+1);
	for (unsigned short int i=0;i<length;i++){
		out->name[i]=name[i];
	}
	out->name[length]=0;
	out->name_length=length;
	out->type=type;
	out->ref_cnt=1;
	switch (type){
		case VFS_NODE_TYPE_DATA:
			out->data.data=NULL;
			out->data.length=0;
			break;
		case VFS_NODE_TYPE_LINK:
			out->link.link=NULL;
			break;
		case VFS_NODE_TYPE_DIRECTORY:
			out->directory.first_entry=NULL;
			break;
		default:
			_error("_alloc_node: Invalid type");
			break;
	}
	out->parent=NULL;
	out->prev_sibling=NULL;
	out->next_sibling=NULL;
	return out;
}



static void _release_node(vfs_node_t* node){
	node->ref_cnt--;
	if (node->ref_cnt){
		return;
	}
	if (node->parent){
		if (node->parent->directory.first_entry==node){
			node->parent->directory.first_entry=(node->next_sibling?node->next_sibling:node->prev_sibling);
		}
		if (node->next_sibling){
			node->next_sibling->prev_sibling=node->prev_sibling;
		}
		if (node->prev_sibling){
			node->prev_sibling->next_sibling=node->next_sibling;
		}
		_release_node(node->parent);
	}
	if (node->type==VFS_NODE_TYPE_DIRECTORY){
		if (node->directory.first_entry){
			_error("directory not empty");
		}
	}
	if (node->type==VFS_NODE_TYPE_LINK){
		free(node->link.link);
	}
	free(node->name);
	free(node);
}



static void _release_node_with_children(vfs_node_t* node){
	if (node->type==VFS_NODE_TYPE_DIRECTORY){
		vfs_node_t* child=node->directory.first_entry;
		while (child){
			vfs_node_t* next_child=child->next_sibling;
			_release_node_with_children(child);
			child=next_child;
		}
	}
	_release_node(node);
}



static void _add_vfs_node(vfs_node_t* directory,vfs_node_t* node){
	if (directory->type!=VFS_NODE_TYPE_DIRECTORY){
		_error("_add_vfs_node requires a VFS_NODE_TYPE_DIRECTORY node");
		return;
	}
	directory->ref_cnt++;
	node->parent=directory;
	node->prev_sibling=NULL;
	node->next_sibling=directory->directory.first_entry;
	if (node->next_sibling){
		node->next_sibling->prev_sibling=node;
	}
	directory->directory.first_entry=node;
}



static void _set_link_target(vfs_node_t* node,const char* link_target){
	if (node->type!=VFS_NODE_TYPE_LINK){
		_error("_set_link_target requires a VFS_NODE_TYPE_LINK node");
		return;
	}
	free(node->link.link);
	node->link.link=NULL;
	if (!link_target){
		return;
	}
	unsigned short int length=0;
	while (link_target[length]){
		length++;
	}
	node->link.link=malloc(length+1);
	for (unsigned short int i=0;i<length;i++){
		node->link.link[i]=link_target[i];
	}
	node->link.link[length]=0;
}



static vfs_node_t* _lookup_vfs_node(const vfs_node_t* node,const char* name,unsigned short int length){
	if (node->type!=VFS_NODE_TYPE_DIRECTORY){
		return NULL;
	}
	for (vfs_node_t* entry=node->directory.first_entry;entry;entry=entry->next_sibling){
		if (entry->name_length!=length){
			continue;
		}
		for (unsigned short int i=0;i<length;i++){
			if (entry->name[i]!=name[i]){
				goto _next_entry;
			}
		}
		return entry;
_next_entry:
	}
	return NULL;
}



static vfs_fd_t _alloc_descriptor(vfs_node_t* node,vfs_flags_t flags){
	unsigned int i=0;
	while (!_vfs_fd_unalloc_map[i]){
		i++;
		if (i==(VFS_MAX_FD>>6)){
			return VFS_FD_ERROR;
		}
	}
	vfs_fd_t fd=(i<<6)+__builtin_ffsll(_vfs_fd_unalloc_map[i])-1;
	_vfs_fd_unalloc_map[i]&=_vfs_fd_unalloc_map[i]-1;
	vfs_file_descriptor_t* out=malloc(sizeof(vfs_file_descriptor_t));
	out->prev=NULL;
	out->next=_vfs_root_fd;
	if (_vfs_root_fd){
		_vfs_root_fd->prev=out;
	}
	out->node=node;
	out->flags=flags;
	out->fd=fd;
	out->offset=((flags&VFS_FLAG_APPEND)?node->data.length:0);
	node->ref_cnt++;
	_vfs_root_fd=out;
	return fd;
}



static void _dealloc_descriptor(vfs_file_descriptor_t* fd_data){
	if (fd_data->next){
		fd_data->next->prev=fd_data->prev;
	}
	if (fd_data->prev){
		fd_data->prev->next=fd_data->next;
	}
	if (_vfs_root_fd==fd_data){
		_vfs_root_fd=(fd_data->next?_vfs_root_fd->next:_vfs_root_fd->prev);
	}
	_vfs_fd_unalloc_map[fd_data->fd>>6]|=1ull<<(fd_data->fd&63);
	_release_node(fd_data->node);
	free(fd_data);
}



static vfs_file_descriptor_t* _lookup_descriptor(vfs_fd_t fd){
	if (fd>=VFS_MAX_FD||(_vfs_fd_unalloc_map[fd>>6]&(1ull<<(fd&63)))){
		return NULL;
	}
	vfs_file_descriptor_t* out=_vfs_root_fd;
	while (out->fd!=fd){
		out=out->next;
	}
	return out;
}



void vfs_init(void){
	_vfs_root_node=_alloc_node("/",1,VFS_NODE_TYPE_DIRECTORY);
	_vfs_root_fd=NULL;
	for (unsigned int i=0;i<(VFS_MAX_FD>>6);i++){
		_vfs_fd_unalloc_map[i]=0xffffffffffffffffull;
	}
}



void vfs_deinit(void){
	while (_vfs_root_fd){
		_dealloc_descriptor(_vfs_root_fd);
	}
	_release_node_with_children(_vfs_root_node);
}



vfs_fd_t vfs_open(const char* path,vfs_flags_t flags,const char* link_target){
	if ((flags&(VFS_FLAG_DIRECTORY|VFS_FLAG_LINK))==(VFS_FLAG_DIRECTORY|VFS_FLAG_LINK)){
		_error("Invalid flags");
		return VFS_FD_ERROR;
	}
	unsigned int link_count=0;
_retry_lookup:
	if (path[0]!='/'){
		_error("File not found");
		return VFS_FD_ERROR;
	}
	vfs_node_t* node=_vfs_root_node;
	_Bool was_node_created=0;
	do{
		path++;
		unsigned int i=0;
		while (path[i]&&path[i]!='/'){
			i++;
		}
		if (!i){
			break;
		}
		vfs_node_t* child=_lookup_vfs_node(node,path,i);
		if (!child){
			if (path[i]||!(flags&VFS_FLAG_CREATE)||node->type!=VFS_NODE_TYPE_DIRECTORY){
				_error("File not found");
				return VFS_FD_ERROR;
			}
			child=_alloc_node(path,i,((flags&VFS_FLAG_DIRECTORY)?VFS_NODE_TYPE_DIRECTORY:((flags&VFS_FLAG_LINK)?VFS_NODE_TYPE_LINK:VFS_NODE_TYPE_DATA)));
			if (flags&VFS_FLAG_LINK){
				_set_link_target(child,link_target);
			}
			_add_vfs_node(node,child);
			was_node_created=1;
		}
		node=child;
		path+=i;
	} while (path[0]);
	if (!was_node_created&&node->type==VFS_NODE_TYPE_LINK&&!(flags&VFS_FLAG_IGNORE_LINKS)&&node->link.link){
		path=node->link.link;
		link_count++;
		if (link_count>=MAX_LINK_FOLLOW_COUNT){
			_error("Too many links");
			return VFS_FD_ERROR;
		}
		goto _retry_lookup;
	}
	if (flags&_invalid_flags[node->type]){
		_error("Invalid flags");
		goto _error_cleanup;
	}
	if (flags&VFS_FLAG_APPEND){
		flags|=VFS_FLAG_WRITE;
	}
	vfs_fd_t out=_alloc_descriptor(node,flags);
	if (out!=VFS_FD_ERROR){
		return out;
	}
_error_cleanup:
	if (was_node_created){
		_release_node(node);
	}
	return VFS_FD_ERROR;
}



_Bool vfs_close(vfs_fd_t fd){
	if (fd==VFS_FD_ERROR){
		return 0;
	}
	vfs_file_descriptor_t* fd_data=_lookup_descriptor(fd);
	if (!fd_data){
		_error("Unknown file descriptor");
		return 0;
	}
	_dealloc_descriptor(fd_data);
	return 1;
}



_Bool vfs_unlink(vfs_fd_t fd){
	if (fd==VFS_FD_ERROR){
		return 0;
	}
	vfs_file_descriptor_t* fd_data=_lookup_descriptor(fd);
	if (!fd_data){
		_error("Unknown file descriptor");
		return 0;
	}
	if (fd_data->node->type==VFS_NODE_TYPE_DIRECTORY&&fd_data->node->directory.first_entry){
		_error("Directory not empty");
		return 0;
	}
	_release_node(fd_data->node);
	_dealloc_descriptor(fd_data);
	return 1;
}



unsigned int vfs_read(vfs_fd_t fd,void* buffer,unsigned int count);



unsigned int vfs_write(vfs_fd_t fd,const void* buffer,unsigned int count);



vfs_offset_t vfs_seek(vfs_fd_t fd,vfs_offset_t offset,vfs_flags_t flags){
	if (fd==VFS_FD_ERROR){
		return VFS_OFFSET_ERROR;
	}
	vfs_file_descriptor_t* fd_data=_lookup_descriptor(fd);
	if (!fd_data){
		_error("Unknown file descriptor");
		return VFS_OFFSET_ERROR;
	}
	if (fd_data->node->type!=VFS_NODE_TYPE_DATA){
		_error("Not a regular file");
		return VFS_OFFSET_ERROR;
	}
	switch (flags){
		case VFS_FLAG_SEEK_SET:
			fd_data->offset=offset;
			break;
		case VFS_FLAG_SEEK_ADD:
			fd_data->offset+=offset;
			break;
		case VFS_FLAG_SEEK_END:
			fd_data->offset=fd_data->node->data.length;
			break;
		default:
			_error("Invalid flags");
			return VFS_OFFSET_ERROR;
	}
	return fd_data->offset;
}



const char* vfs_read_link(vfs_fd_t fd){
	if (fd==VFS_FD_ERROR){
		return NULL;
	}
	vfs_file_descriptor_t* fd_data=_lookup_descriptor(fd);
	if (!fd_data){
		_error("Unknown file descriptor");
		return NULL;
	}
	if (fd_data->node->type!=VFS_NODE_TYPE_LINK){
		_error("Not a link");
		return NULL;
	}
	return fd_data->node->link.link;
}



_Bool vfs_write_link(vfs_fd_t fd,const char* path){
	if (fd==VFS_FD_ERROR){
		return 0;
	}
	vfs_file_descriptor_t* fd_data=_lookup_descriptor(fd);
	if (!fd_data){
		_error("Unknown file descriptor");
		return 0;
	}
	if (fd_data->node->type!=VFS_NODE_TYPE_LINK){
		_error("Not a link");
		return 0;
	}
	_set_link_target(fd_data->node,path);
	return 1;
}



_Bool vfs_read_dir(vfs_fd_t fd,vfs_dir_entry_t* entry){
	if (fd==VFS_FD_ERROR){
		return 0;
	}
	if (entry->fd==VFS_FD_ERROR){
		vfs_file_descriptor_t* fd_data=_lookup_descriptor(fd);
		if (!fd_data){
			_error("Unknown file descriptor");
			return 0;
		}
		if (fd_data->node->type!=VFS_NODE_TYPE_DIRECTORY){
			_error("Not a directory");
			return 0;
		}
		if (!fd_data->node->directory.first_entry){
			return 0;
		}
		vfs_node_t* node=fd_data->node->directory.first_entry;
		entry->name=malloc(node->name_length+1);
		for (unsigned int i=0;i<node->name_length+1;i++){
			entry->name[i]=node->name[i];
		}
		entry->fd=_alloc_descriptor(node,0);
		return 1;
	}
	vfs_file_descriptor_t* fd_data=_lookup_descriptor(entry->fd);
	if (!fd_data){
		_error("Unknown file descriptor");
		return 0;
	}
	free(entry->name);
	vfs_node_t* node=fd_data->node->next_sibling;
	if (node){
		entry->name=malloc(node->name_length+1);
		for (unsigned int i=0;i<node->name_length+1;i++){
			entry->name[i]=node->name[i];
		}
		fd_data->node=node;
		return 1;
	}
	_dealloc_descriptor(fd_data);
	entry->name=NULL;
	entry->fd=VFS_FD_ERROR;
	return 0;
}



unsigned int vfs_absolute_path(vfs_fd_t fd,char* buffer,unsigned int buffer_length);

