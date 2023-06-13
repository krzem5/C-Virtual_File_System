// add one fd that is alays internally reserved to allow for fast [vfs_open, XXX, vfs_close] operations
// implement vfs_read and vfs_write
// implement vfs_dup
// add vfs_stat_t to vfs_dir_entry_t



#include <vfs/vfs.h>
#include <stdio.h>



static void tree(vfs_fd_t fd,unsigned int indent){
	if (fd==VFS_FD_ERROR){
		fd=vfs_open("/",0,NULL);
		tree(fd,0);
		vfs_close(fd);
		return;
	}
	vfs_stat_t stat;
	if (!vfs_stat(fd,&stat)){
		return;
	}
	for (unsigned int i=0;i<indent;i++){
		putchar(' ');
		putchar(' ');
	}
	if (stat.type==VFS_NODE_TYPE_DATA){
		printf("%s\n",stat.name);
		return;
	}
	if (stat.type==VFS_NODE_TYPE_LINK){
		printf("%s -> %s\n",stat.name,vfs_read_link(fd));
		return;
	}
	printf("%s/\n",stat.name);
	for (vfs_dir_entry_t entry=VFS_DIR_ENTRY_INIT;vfs_read_dir(fd,&entry);){
		tree(entry.fd,indent+1);
	}
}



int main(void){
	vfs_init();
	vfs_fd_t i=vfs_open("/a",VFS_FLAG_WRITE|VFS_FLAG_CREATE,NULL);
	vfs_fd_t j=vfs_open("/b",VFS_FLAG_WRITE,NULL);
	printf("%u,%u\n",i,j);
	vfs_close(i);
	vfs_close(j);
	i=vfs_open("/dir",VFS_FLAG_CREATE|VFS_FLAG_DIRECTORY,NULL);
	j=vfs_open("/dir/test",VFS_FLAG_CREATE,NULL);
	vfs_fd_t k=vfs_open("/dir/test",0,NULL);
	tree(VFS_FD_ERROR,0);
	char buffer[VFS_MAX_PATH];
	vfs_absolute_path(k,buffer,VFS_MAX_PATH);
	printf("[k]: %s\n",buffer);
	vfs_unlink(j);
	vfs_unlink(k);
	vfs_unlink(i);
	vfs_close(vfs_open("/lnk_to_a",VFS_FLAG_CREATE|VFS_FLAG_LINK,"/a"));
	vfs_unlink(vfs_open("/lnk_to_a",0,NULL));
	i=vfs_open("/lnk_to_a",VFS_FLAG_IGNORE_LINKS,NULL);
	printf("Link: %s\n",vfs_read_link(i));
	vfs_write_link(i,"/b");
	printf("Link: %s\n",vfs_read_link(i));
	vfs_open("/lnk_to_a",0,NULL);
	vfs_stat_t stat;
	vfs_stat(i,&stat);
	printf("%u | %u\n",stat.type,stat.size);
	vfs_close(i);
	vfs_deinit();
	return 0;
}
