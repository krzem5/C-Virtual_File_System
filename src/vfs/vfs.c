#include <stddef.h>
#include <stdlib.h>
#include <vfs/vfs.h>



static vfs_node_t* _vfs_root_node=NULL;
static vfs_file_descriptor_t* _vfs_root_fd=NULL;
static unsigned long long int _vfs_fd_unalloc_map[VFS_MAX_FD>>6];
static vfs_fd_t _vfs_temp_fd=VFS_FD_ERROR;
static vfs_error_t _vfs_error;



static inline _Bool _check_flags(vfs_flags_t flags,vfs_flags_t valid_flags){
	if (flags&(~valid_flags)){
		_vfs_error=VFS_ERROR_INVALID_FLAGS;
		return 1;
	}
	return 0;
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
			_vfs_error=VFS_ERROR_INTERNAL;
			break;
	}
	out->parent=NULL;
	out->prev_sibling=NULL;
	out->next_sibling=NULL;
	return out;
}



static void _release_node(vfs_node_t* node){
	node->ref_cnt--;
	if (node->ref_cnt&VFS_REF_CNT_COUNT_MASK){
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
			_vfs_error=VFS_ERROR_DIRECTORY_NOT_EMPTY;
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
		_vfs_error=VFS_ERROR_OPERATION_NOT_SUPPORTED;
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
		_vfs_error=VFS_ERROR_OPERATION_NOT_SUPPORTED;
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



static vfs_node_t* _lookup_vfs_node(vfs_node_t* node,const char* name,unsigned short int length){
	if (node->type!=VFS_NODE_TYPE_DIRECTORY){
		return NULL;
	}
	if (length==1&&name[0]=='.'){
		return node;
	}
	if (length==2&&name[0]=='.'&&name[1]=='.'){
		return (node->parent?node->parent:node);
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



static vfs_fd_t _alloc_descriptor(vfs_fd_t fd,vfs_node_t* node,vfs_flags_t flags,vfs_offset_t offset){
	node->ref_cnt++;
	vfs_file_descriptor_t* fd_data;
	if (fd!=VFS_FD_ERROR){
		fd_data=_lookup_descriptor(fd);
		_release_node(fd_data->node);
	}
	else if (_vfs_temp_fd!=VFS_FD_ERROR){
		fd_data=_lookup_descriptor(_vfs_temp_fd);
		_vfs_temp_fd=VFS_FD_ERROR;
	}
	else{
		unsigned int i=0;
		while (!_vfs_fd_unalloc_map[i]){
			i++;
			if (i==(VFS_MAX_FD>>6)){
				node->ref_cnt--;
				return VFS_FD_ERROR;
			}
		}
		fd=(i<<6)+__builtin_ffsll(_vfs_fd_unalloc_map[i])-1;
		_vfs_fd_unalloc_map[i]&=_vfs_fd_unalloc_map[i]-1;
		fd_data=malloc(sizeof(vfs_file_descriptor_t));
		fd_data->prev=NULL;
		fd_data->next=_vfs_root_fd;
		fd_data->fd=fd;
		if (_vfs_root_fd){
			_vfs_root_fd->prev=fd_data;
		}
		_vfs_root_fd=fd_data;
	}
	fd_data->node=node;
	fd_data->flags=flags;
	fd_data->offset=(offset==VFS_OFFSET_ERROR?((flags&VFS_FLAG_APPEND)?node->data.length:0):offset);
	return fd_data->fd;
}



static void _dealloc_descriptor(vfs_file_descriptor_t* fd_data,_Bool fill_temporary_fd){
	if (fd_data->node){
		_release_node(fd_data->node);
		fd_data->node=NULL;
	}
	if (fill_temporary_fd&&_vfs_temp_fd==VFS_FD_ERROR){
		_vfs_temp_fd=fd_data->fd;
		return;
	}
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
	free(fd_data);
}



static void _get_node_data(vfs_fd_t fd,const vfs_node_t* node,vfs_stat_t* stat){
	stat->fd=fd;
	for (unsigned short int i=0;i<=node->name_length;i++){
		stat->name[i]=node->name[i];
	}
	stat->name_length=node->name_length;
	switch (node->type){
		case VFS_NODE_TYPE_DATA:
			stat->size=(node->data.length+VFS_MAX_PATH-1)&(~VFS_MAX_PATH);
			break;
		case VFS_NODE_TYPE_LINK:
		case VFS_NODE_TYPE_DIRECTORY:
			stat->size=VFS_MAX_PATH;
			break;
	}
	stat->type=node->type;
}



void vfs_init(void){
	_vfs_root_node=_alloc_node("",0,VFS_NODE_TYPE_DIRECTORY);
	_vfs_root_fd=NULL;
	for (unsigned int i=0;i<(VFS_MAX_FD>>6);i++){
		_vfs_fd_unalloc_map[i]=0xffffffffffffffffull;
	}
	_vfs_temp_fd=VFS_FD_ERROR;
	_vfs_error=VFS_ERROR_NO_ERROR;
}



void vfs_deinit(void){
	_vfs_error=VFS_ERROR_NO_ERROR;
	_vfs_temp_fd=VFS_FD_ERROR;
	while (_vfs_root_fd){
		_dealloc_descriptor(_vfs_root_fd,0);
	}
	_release_node_with_children(_vfs_root_node);
	_vfs_root_node=NULL;
}



vfs_error_t vfs_get_error(void){
	return _vfs_error;
}



vfs_error_t vfs_set_error(vfs_error_t error){
	vfs_error_t out=_vfs_error;
	_vfs_error=error;
	return out;
}



vfs_fd_t vfs_open(const char* path,vfs_flags_t flags,vfs_fd_t fd,const char* link_target){
	_vfs_error=VFS_ERROR_NO_ERROR;
	if ((flags&(VFS_FLAG_DIRECTORY|VFS_FLAG_LINK))==(VFS_FLAG_DIRECTORY|VFS_FLAG_LINK)){
		_vfs_error=VFS_ERROR_INVALID_FLAGS;
		return VFS_FD_ERROR;
	}
	if (flags&VFS_FLAG_DIRECTORY){
		if (_check_flags(flags,VFS_FLAG_CREATE|VFS_FLAG_DIRECTORY|VFS_FLAG_IGNORE_LINKS|VFS_FLAG_REPLACE_FD)){
			return VFS_FD_ERROR;
		}
	}
	else if (flags&VFS_FLAG_LINK){
		if (_check_flags(flags,VFS_FLAG_CREATE|VFS_FLAG_LINK|VFS_FLAG_IGNORE_LINKS|VFS_FLAG_REPLACE_FD)){
			return VFS_FD_ERROR;
		}
	}
	else{
		if (_check_flags(flags,VFS_FLAG_READ|VFS_FLAG_WRITE|VFS_FLAG_APPEND|VFS_FLAG_CREATE|VFS_FLAG_IGNORE_LINKS|VFS_FLAG_REPLACE_FD)){
			return VFS_FD_ERROR;
		}
	}
	unsigned int link_count=0;
_retry_lookup:
	if (path[0]!='/'){
		_vfs_error=VFS_ERROR_FILE_NOT_FOUND;
		return VFS_FD_ERROR;
	}
	vfs_node_t* node=_vfs_root_node;
	_Bool was_node_created=0;
	do{
		do{
			path++;
		} while (path[0]=='/');
		unsigned int i=0;
		while (path[i]&&path[i]!='/'){
			i++;
		}
		if (!i){
			if (path[-1]=='/'){
				if (node->type!=VFS_NODE_TYPE_DIRECTORY){
					_vfs_error=VFS_ERROR_FILE_NOT_FOUND;
					return VFS_FD_ERROR;
				}
			}
			break;
		}
		vfs_node_t* child=_lookup_vfs_node(node,path,i);
		if (!child){
			if (path[i]||!(flags&VFS_FLAG_CREATE)||node->type!=VFS_NODE_TYPE_DIRECTORY){
				_vfs_error=VFS_ERROR_FILE_NOT_FOUND;
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
		if (link_count>=VFS_MAX_LINK_FOLLOW_COUNT){
			_vfs_error=VFS_ERROR_TOO_MANY_LINKS;
			return VFS_FD_ERROR;
		}
		goto _retry_lookup;
	}
	if (flags&VFS_FLAG_APPEND){
		flags|=VFS_FLAG_WRITE;
	}
	vfs_fd_t out=_alloc_descriptor(((flags&VFS_FLAG_REPLACE_FD)?fd:VFS_FD_ERROR),node,flags,VFS_OFFSET_ERROR);
	if (out!=VFS_FD_ERROR){
		return out;
	}
	if (was_node_created){
		_release_node(node);
	}
	return VFS_FD_ERROR;
}



_Bool vfs_close(vfs_fd_t fd){
	_vfs_error=VFS_ERROR_NO_ERROR;
	if (fd==VFS_FD_ERROR){
		_vfs_error=VFS_ERROR_UNKNOWN_FILE_DESCRIPTOR;
		return 0;
	}
	vfs_file_descriptor_t* fd_data=_lookup_descriptor(fd);
	if (!fd_data){
		_vfs_error=VFS_ERROR_UNKNOWN_FILE_DESCRIPTOR;
		return 0;
	}
	_dealloc_descriptor(fd_data,1);
	return 1;
}



_Bool vfs_unlink(vfs_fd_t fd){
	_vfs_error=VFS_ERROR_NO_ERROR;
	if (fd==VFS_FD_ERROR){
		_vfs_error=VFS_ERROR_UNKNOWN_FILE_DESCRIPTOR;
		return 0;
	}
	vfs_file_descriptor_t* fd_data=_lookup_descriptor(fd);
	if (!fd_data){
		_vfs_error=VFS_ERROR_UNKNOWN_FILE_DESCRIPTOR;
		return 0;
	}
	if (fd_data->node->type==VFS_NODE_TYPE_DIRECTORY&&fd_data->node->directory.first_entry){
		_vfs_error=VFS_ERROR_DIRECTORY_NOT_EMPTY;
		return 0;
	}
	if (!(fd_data->node->ref_cnt&VFS_REF_CNT_FLAG_UNLINKED)){
		fd_data->node->ref_cnt|=VFS_REF_CNT_FLAG_UNLINKED;
		_release_node(fd_data->node);
	}
	_dealloc_descriptor(fd_data,1);
	return 1;
}



vfs_offset_t vfs_read(vfs_fd_t fd,void* buffer,vfs_offset_t count){
	_vfs_error=VFS_ERROR_NO_ERROR;
	if (fd==VFS_FD_ERROR){
		_vfs_error=VFS_ERROR_UNKNOWN_FILE_DESCRIPTOR;
		return VFS_OFFSET_ERROR;
	}
	vfs_file_descriptor_t* fd_data=_lookup_descriptor(fd);
	if (!fd_data){
		_vfs_error=VFS_ERROR_UNKNOWN_FILE_DESCRIPTOR;
		return VFS_OFFSET_ERROR;
	}
	if (!(fd_data->flags&VFS_FLAG_READ)){
		_vfs_error=VFS_ERROR_OPERATION_NOT_SUPPORTED;
		return VFS_OFFSET_ERROR;
	}
	vfs_node_t* node=fd_data->node;
	if (fd_data->offset+(unsigned long long int)(count)>node->data.length){
		count=node->data.length-fd_data->offset;
	}
	unsigned char* buffer_data=buffer;
	for (vfs_offset_t i=0;i<count;i++){
		buffer_data[i]=node->data.data[fd_data->offset+i];
	}
	fd_data->offset+=count;
	return count;
}



vfs_offset_t vfs_write(vfs_fd_t fd,const void* buffer,vfs_offset_t count){
	_vfs_error=VFS_ERROR_NO_ERROR;
	if (fd==VFS_FD_ERROR){
		_vfs_error=VFS_ERROR_UNKNOWN_FILE_DESCRIPTOR;
		return VFS_OFFSET_ERROR;
	}
	vfs_file_descriptor_t* fd_data=_lookup_descriptor(fd);
	if (!fd_data){
		_vfs_error=VFS_ERROR_UNKNOWN_FILE_DESCRIPTOR;
		return VFS_OFFSET_ERROR;
	}
	if (!(fd_data->flags&VFS_FLAG_WRITE)){
		_vfs_error=VFS_ERROR_OPERATION_NOT_SUPPORTED;
		return VFS_OFFSET_ERROR;
	}
	vfs_node_t* node=fd_data->node;
	if (fd_data->offset+(unsigned long long int)(count)>0xfffffffeull){
		count=0xfffffffe -fd_data->offset;
	}
	vfs_offset_t offset=fd_data->offset;
	fd_data->offset+=count;
	if (fd_data->offset>node->data.length){
		node->data.length=fd_data->offset;
		node->data.data=realloc(node->data.data,node->data.length);
	}
	const unsigned char* buffer_data=buffer;
	for (unsigned int i=0;i<count;i++){
		node->data.data[offset+i]=buffer_data[i];
	}
	return count;
}



vfs_offset_t vfs_seek(vfs_fd_t fd,vfs_offset_t offset,vfs_flags_t flags){
	_vfs_error=VFS_ERROR_NO_ERROR;
	if (fd==VFS_FD_ERROR){
		_vfs_error=VFS_ERROR_UNKNOWN_FILE_DESCRIPTOR;
		return VFS_OFFSET_ERROR;
	}
	vfs_file_descriptor_t* fd_data=_lookup_descriptor(fd);
	if (!fd_data){
		_vfs_error=VFS_ERROR_UNKNOWN_FILE_DESCRIPTOR;
		return VFS_OFFSET_ERROR;
	}
	if (fd_data->node->type!=VFS_NODE_TYPE_DATA){
		_vfs_error=VFS_ERROR_OPERATION_NOT_SUPPORTED;
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
			_vfs_error=VFS_ERROR_INVALID_FLAGS;
			return VFS_OFFSET_ERROR;
	}
	if (fd_data->offset>fd_data->node->data.length){
		fd_data->offset=fd_data->node->data.length;
	}
	return fd_data->offset;
}



unsigned int vfs_read_link(vfs_fd_t fd,char* buffer,unsigned int buffer_length){
	_vfs_error=VFS_ERROR_NO_ERROR;
	if (fd==VFS_FD_ERROR){
		_vfs_error=VFS_ERROR_UNKNOWN_FILE_DESCRIPTOR;
		return 0;
	}
	vfs_file_descriptor_t* fd_data=_lookup_descriptor(fd);
	if (!fd_data){
		_vfs_error=VFS_ERROR_UNKNOWN_FILE_DESCRIPTOR;
		return 0;
	}
	if (fd_data->node->type!=VFS_NODE_TYPE_LINK){
		_vfs_error=VFS_ERROR_OPERATION_NOT_SUPPORTED;
		return 0;
	}
	if (!buffer_length){
		return 0;
	}
	const char* link=fd_data->node->link.link;
	if (!link){
		if (buffer_length){
			buffer[0]=0;
		}
		return 0;
	}
	if (buffer_length==1){
		buffer[0]=0;
		return 1;
	}
	unsigned int length=0;
	while (1){
		if (length>=buffer_length){
			buffer[length-1]=0;
			return length-2;
		}
		buffer[length]=link[length];
		if (!link[length]){
			break;
		}
		length++;
	}
	return length;
}



_Bool vfs_write_link(vfs_fd_t fd,const char* path){
	_vfs_error=VFS_ERROR_NO_ERROR;
	if (fd==VFS_FD_ERROR){
		_vfs_error=VFS_ERROR_UNKNOWN_FILE_DESCRIPTOR;
		return 0;
	}
	vfs_file_descriptor_t* fd_data=_lookup_descriptor(fd);
	if (!fd_data){
		_vfs_error=VFS_ERROR_UNKNOWN_FILE_DESCRIPTOR;
		return 0;
	}
	if (fd_data->node->type!=VFS_NODE_TYPE_LINK){
		_vfs_error=VFS_ERROR_OPERATION_NOT_SUPPORTED;
		return 0;
	}
	_set_link_target(fd_data->node,path);
	return 1;
}



_Bool vfs_get_relative(vfs_fd_t fd,vfs_flags_t flags,vfs_stat_t* stat){
	_vfs_error=VFS_ERROR_NO_ERROR;
	if (fd==VFS_FD_ERROR){
		_vfs_error=VFS_ERROR_UNKNOWN_FILE_DESCRIPTOR;
		return 0;
	}
	vfs_file_descriptor_t* fd_data=_lookup_descriptor(fd);
	if (!fd_data){
		_vfs_error=VFS_ERROR_UNKNOWN_FILE_DESCRIPTOR;
		return 0;
	}
	vfs_node_t* node=fd_data->node;
	switch (flags&(~VFS_FLAG_REPLACE_FD)){
		case VFS_FLAG_RELATIVE_PARENT:
			if (node->parent){
				node=node->parent;
			}
			break;
		case VFS_FLAG_RELATIVE_CHILD:
			if (node->type!=VFS_NODE_TYPE_DIRECTORY){
				_vfs_error=VFS_ERROR_OPERATION_NOT_SUPPORTED;
				return 0;
			}
			node=node->directory.first_entry;
			break;
		case VFS_FLAG_RELATIVE_NEXT_SIBLING:
			node=node->next_sibling;
			break;
		case VFS_FLAG_RELATIVE_PREV_SIBLING:
			node=node->prev_sibling;
			break;
		default:
			_vfs_error=VFS_ERROR_INVALID_FLAGS;
			return 0;
	}
	if (!node){
		if (flags&VFS_FLAG_REPLACE_FD){
			_dealloc_descriptor(fd_data,1);
		}
		return 0;
	}
	stat->fd=_alloc_descriptor(((flags&VFS_FLAG_REPLACE_FD)?fd:VFS_FD_ERROR),node,0,VFS_OFFSET_ERROR);
	_get_node_data(stat->fd,node,stat);
	return 1;
}



unsigned int vfs_absolute_path(vfs_fd_t fd,char* buffer,unsigned int buffer_length){
	_vfs_error=VFS_ERROR_NO_ERROR;
	if (fd==VFS_FD_ERROR){
		_vfs_error=VFS_ERROR_UNKNOWN_FILE_DESCRIPTOR;
		return 0;
	}
	vfs_file_descriptor_t* fd_data=_lookup_descriptor(fd);
	if (!fd_data){
		_vfs_error=VFS_ERROR_UNKNOWN_FILE_DESCRIPTOR;
		return 0;
	}
	if (!buffer||!buffer_length){
		return 0;
	}
	unsigned int i=buffer_length-1;
	buffer[i]=0;
	for (const vfs_node_t* node=fd_data->node;1;node=node->parent){
		if (i<node->name_length){
			return 0;
		}
		i-=node->name_length;
		for (unsigned short int j=0;j<node->name_length;j++){
			buffer[i+j]=node->name[j];
		}
		if (!node->parent){
			break;
		}
		if (!i){
			return 0;
		}
		i--;
		buffer[i]='/';
	}
	if (!i){
		return buffer_length-1;
	}
	for (unsigned int j=0;j<buffer_length-i;j++){
		buffer[j]=buffer[i+j];
	}
	return buffer_length-i;
}



_Bool vfs_stat(vfs_fd_t fd,vfs_flags_t flags,vfs_stat_t* stat){
	_vfs_error=VFS_ERROR_NO_ERROR;
	if (fd==VFS_FD_ERROR){
		_vfs_error=VFS_ERROR_UNKNOWN_FILE_DESCRIPTOR;
		return 0;
	}
	vfs_file_descriptor_t* fd_data=_lookup_descriptor(fd);
	if (!fd_data){
		_vfs_error=VFS_ERROR_UNKNOWN_FILE_DESCRIPTOR;
		return 0;
	}
	vfs_node_t* node=fd_data->node;
	vfs_flags_t rwa_flags=flags&(VFS_FLAG_READ|VFS_FLAG_WRITE|VFS_FLAG_APPEND);
	if (rwa_flags&VFS_FLAG_APPEND){
		rwa_flags|=VFS_FLAG_WRITE;
	}
	switch (flags&(~(VFS_FLAG_READ|VFS_FLAG_WRITE|VFS_FLAG_APPEND|VFS_FLAG_REPLACE_FD))){
		case VFS_FLAG_RELATIVE_PARENT:
			if (node->parent){
				node=node->parent;
				break;
			}
		case 0:
			if (rwa_flags){
				_vfs_error=VFS_ERROR_OPERATION_NOT_SUPPORTED;
				return 0;
			}
			else{
				stat->fd=fd;
			}
			goto _skip_descriptor_allocation;
		case VFS_FLAG_RELATIVE_CHILD:
			if (node->type!=VFS_NODE_TYPE_DIRECTORY){
				_vfs_error=VFS_ERROR_OPERATION_NOT_SUPPORTED;
				return 0;
			}
			node=node->directory.first_entry;
			break;
		case VFS_FLAG_RELATIVE_NEXT_SIBLING:
			node=node->next_sibling;
			break;
		case VFS_FLAG_RELATIVE_PREV_SIBLING:
			node=node->prev_sibling;
			break;
		default:
			_vfs_error=VFS_ERROR_INVALID_FLAGS;
			return 0;
	}
	if (!node){
		if (flags&VFS_FLAG_REPLACE_FD){
			_dealloc_descriptor(fd_data,1);
		}
		return 0;
	}
	stat->fd=_alloc_descriptor(((flags&VFS_FLAG_REPLACE_FD)?fd:VFS_FD_ERROR),node,rwa_flags,VFS_OFFSET_ERROR);
_skip_descriptor_allocation:
	_get_node_data(stat->fd,node,stat);
	return 1;
}



vfs_fd_t vfs_dup(vfs_fd_t fd,vfs_flags_t flags,vfs_fd_t target_fd){
	_vfs_error=VFS_ERROR_NO_ERROR;
	if (fd==VFS_FD_ERROR){
		_vfs_error=VFS_ERROR_UNKNOWN_FILE_DESCRIPTOR;
		return VFS_FD_ERROR;
	}
	vfs_file_descriptor_t* fd_data=_lookup_descriptor(fd);
	if (!fd_data){
		_vfs_error=VFS_ERROR_UNKNOWN_FILE_DESCRIPTOR;
		return VFS_FD_ERROR;
	}
	vfs_node_t* node=fd_data->node;
	if (_check_flags(flags,(node->type==VFS_NODE_TYPE_DATA?VFS_FLAG_READ|VFS_FLAG_WRITE|VFS_FLAG_APPEND:0)|VFS_FLAG_REPLACE_FD)){
		return VFS_FD_ERROR;
	}
	return _alloc_descriptor(((flags&VFS_FLAG_REPLACE_FD)?target_fd:VFS_FD_ERROR),node,flags,fd_data->offset);
}
