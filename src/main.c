// add one fd that is alays internally reserved to allow for fast [vfs_open, XXX, vfs_close] operations
// implement vfs_read and vfs_write
// implement vfs_absolute_path
// implement vfs_stat



#include <vfs/vfs.h>
#include <stdio.h>



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
	vfs_close(i);
	vfs_fd_t root=vfs_open("/",0,NULL);
	for (vfs_dir_entry_t entry=VFS_DIR_ENTRY_INIT;vfs_read_dir(root,&entry);){
		printf("/%s\n",entry.name);
	}
	vfs_close(root);
	vfs_deinit();
	return 0;
}
