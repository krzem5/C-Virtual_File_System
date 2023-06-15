#ifndef _VFS_VFS_H_
#define _VFS_VFS_H_ 1
#include <stddef.h>



#define VFS_MAX_PATH 4096

#define VFS_MAX_FD 1024

#define VFS_MAX_LINK_FOLLOW_COUNT 10



#define VFS_ERROR_NO_ERROR 0
#define VFS_ERROR_DIRECTORY_NOT_EMPTY 1
#define VFS_ERROR_FILE_NOT_FOUND 2
#define VFS_ERROR_INVALID_FLAGS 3
#define VFS_ERROR_OPERATION_NOT_SUPPORTED 4
#define VFS_ERROR_TOO_MANY_LINKS 5
#define VFS_ERROR_UNKNOWN_FILE_DESCRIPTOR 6
#define VFS_ERROR_INTERNAL 7

#define VFS_FD_ERROR 0xffff

#define VFS_OFFSET_ERROR 0xffffffff

#define VFS_FLAG_READ 1
#define VFS_FLAG_WRITE 2
#define VFS_FLAG_APPEND 4
#define VFS_FLAG_CREATE 8
#define VFS_FLAG_DIRECTORY 16
#define VFS_FLAG_LINK 32
#define VFS_FLAG_SEEK_SET 64
#define VFS_FLAG_SEEK_ADD 128
#define VFS_FLAG_SEEK_END 256
#define VFS_FLAG_IGNORE_LINKS 512
#define VFS_FLAG_RELATIVE_PARENT 1024
#define VFS_FLAG_RELATIVE_CHILD 2048
#define VFS_FLAG_RELATIVE_NEXT_SIBLING 4096
#define VFS_FLAG_RELATIVE_PREV_SIBLING 8192
#define VFS_FLAG_REPLACE_FD 16384

#define VFS_NODE_TYPE_DATA 0
#define VFS_NODE_TYPE_LINK 1
#define VFS_NODE_TYPE_DIRECTORY 2

#define VFS_DIR_ENTRY_INIT {.fd=VFS_FD_ERROR}

#define VFS_REF_CNT_FLAG_UNLINKED 0x80000000
#define VFS_REF_CNT_COUNT_MASK 0x7fffffff



typedef unsigned char vfs_error_t;



typedef unsigned short int vfs_fd_t;



typedef unsigned int vfs_offset_t;



typedef unsigned int vfs_flags_t;



typedef unsigned char vfs_node_type_t;



typedef struct _VFS_STAT{
	vfs_fd_t fd;
	char name[VFS_MAX_PATH];
	unsigned short int name_length;
	vfs_offset_t size;
	vfs_node_type_t type;
} vfs_stat_t;



typedef vfs_stat_t vfs_dir_entry_t;



typedef struct _VFS_NODE{
	char* name;
	unsigned short int name_length;
	vfs_node_type_t type;
	unsigned int ref_cnt;
	union{
		struct{
			unsigned char* data;
			vfs_offset_t length;
		} data;
		struct{
			char* link;
		} link;
		struct{
			struct _VFS_NODE* first_entry;
		} directory;
	};
	struct _VFS_NODE* parent;
	struct _VFS_NODE* prev_sibling;
	struct _VFS_NODE* next_sibling;
} vfs_node_t;



typedef struct _VFS_FILE_DESCRIPTOR{
	struct _VFS_FILE_DESCRIPTOR* prev;
	struct _VFS_FILE_DESCRIPTOR* next;
	vfs_node_t* node;
	vfs_flags_t flags;
	vfs_fd_t fd;
	vfs_offset_t offset;
} vfs_file_descriptor_t;



void vfs_init(void);



void vfs_deinit(void);



vfs_error_t vfs_get_error(void);



vfs_error_t vfs_set_error(vfs_error_t error);



vfs_fd_t vfs_open(const char* path,vfs_flags_t flags,vfs_fd_t fd,const char* link_target);



_Bool vfs_close(vfs_fd_t fd);



_Bool vfs_unlink(vfs_fd_t fd);



vfs_offset_t vfs_read(vfs_fd_t fd,void* buffer,vfs_offset_t count);



vfs_offset_t vfs_write(vfs_fd_t fd,const void* buffer,vfs_offset_t count);



vfs_offset_t vfs_seek(vfs_fd_t fd,vfs_offset_t offset,vfs_flags_t flags);



unsigned int vfs_read_link(vfs_fd_t fd,char* buffer,unsigned int buffer_length);



_Bool vfs_write_link(vfs_fd_t fd,const char* path);



unsigned int vfs_absolute_path(vfs_fd_t fd,char* buffer,unsigned int buffer_length);



_Bool vfs_stat(vfs_fd_t fd,vfs_flags_t flags,vfs_stat_t* stat);



vfs_fd_t vfs_dup(vfs_fd_t fd,vfs_flags_t flags,vfs_fd_t target_fd);



static inline _Bool vfs_read_dir(vfs_fd_t dir_fd,vfs_stat_t* stat){
	return (stat->fd==VFS_FD_ERROR?vfs_stat(dir_fd,VFS_FLAG_RELATIVE_CHILD,stat):vfs_stat(stat->fd,VFS_FLAG_RELATIVE_NEXT_SIBLING|VFS_FLAG_REPLACE_FD,stat));
}



#endif
