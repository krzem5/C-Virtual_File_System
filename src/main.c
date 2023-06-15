// add flag to dup the fd returned via vfs_stat



#include <vfs/vfs.h>
#include <stdio.h>



static void _tree_recursive(const vfs_stat_t* stat){
	char path[VFS_MAX_PATH];
	if (!vfs_absolute_path(stat->fd,path,VFS_MAX_PATH)){
		return;
	}
	if (stat->type==VFS_NODE_TYPE_DATA){
		printf("%s\n",path);
		return;
	}
	if (stat->type==VFS_NODE_TYPE_LINK){
		printf("%s -> ",path);
		if (vfs_read_link(stat->fd,path,VFS_MAX_PATH)){
			printf("%s\n",path);
		}
		else{
			printf("(no target)\n");
		}
		return;
	}
	printf("%s/\n",path);
	for (vfs_dir_entry_t entry=VFS_DIR_ENTRY_INIT;vfs_read_dir(stat->fd,&entry);){
		_tree_recursive(&entry);
	}
}



static void tree(void){
	vfs_fd_t root=vfs_open("/",0,0,NULL);
	vfs_stat_t stat;
	vfs_stat(root,0,&stat);
	_tree_recursive(&stat);
	vfs_close(root);
}



int main(void){
	vfs_init();
	vfs_fd_t i=vfs_open("/./.././../../a",VFS_FLAG_WRITE|VFS_FLAG_CREATE,0,NULL);
	vfs_fd_t j=vfs_open("/b",VFS_FLAG_WRITE,0,NULL);
	printf("%u,%u\n",i,j);
	vfs_close(i);
	vfs_close(j);
	i=vfs_open("/dir",VFS_FLAG_CREATE|VFS_FLAG_DIRECTORY,0,NULL);
	j=vfs_open("/dir/test",VFS_FLAG_CREATE,0,NULL);
	vfs_fd_t k=vfs_dup(j,0,0);
	tree();
	char buffer[VFS_MAX_PATH];
	vfs_absolute_path(k,buffer,VFS_MAX_PATH);
	printf("[k]: %s\n",buffer);
	vfs_unlink(j);
	vfs_unlink(k);
	vfs_unlink(i);
	vfs_close(vfs_open("/lnk_to_a",VFS_FLAG_CREATE|VFS_FLAG_LINK,0,"/a"));
	vfs_unlink(vfs_open("/lnk_to_a",0,0,NULL));
	i=vfs_open("/lnk_to_a",VFS_FLAG_IGNORE_LINKS,0,NULL);
	char link_target_path[VFS_MAX_PATH];
	link_target_path[vfs_read_link(i,link_target_path,VFS_MAX_PATH)]=0;
	printf("Link: %s\n",link_target_path);
	vfs_write_link(i,"/b");
	link_target_path[vfs_read_link(i,link_target_path,VFS_MAX_PATH)]=0;
	printf("Link: %s\n",link_target_path);
	vfs_open("/lnk_to_a",0,0,NULL);
	vfs_stat_t stat;
	vfs_stat(i,0,&stat);
	printf("type: %u, size: %u\n",stat.type,stat.size);
	vfs_close(i);
	tree();
	i=vfs_open("/a",VFS_FLAG_WRITE|VFS_FLAG_CREATE,0,NULL);
	j=vfs_dup(i,0,0);
	vfs_dup(j,VFS_FLAG_READ|VFS_FLAG_REPLACE_FD,j);
	printf("Write: %u\n",vfs_write(i,"abc_def_ghi",12));
	char data[128];
	printf("Read: %u (%s)\n",vfs_read(j,data,128),data);
	vfs_close(i);
	vfs_open("/lnk_to_a",VFS_FLAG_IGNORE_LINKS|VFS_FLAG_REPLACE_FD,j,NULL);
	printf("%u\n",vfs_get_error());
	vfs_unlink(j);
	tree();
	vfs_deinit();
	return 0;
}
