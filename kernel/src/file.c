#include <malloc.h>
#include <gmalloc.h>
#include <string.h>
#include "driver/fs.h"
#include "cpu.h"
#include <util/event.h>


typedef struct {
	void(*callback)(void* buffer, int len, void* context);
	void*			context;
	void*			buffer;
	File*			file;
} ReadContext;

static File files[FILE_MAX_DESC]; 

static void* alloc_descriptor(void) {
	for(int i = 0; i < FILE_MAX_DESC; i++) {
		if(files[i].type == FILE_TYPE_NONE) {
			files[i].type = FILE_TYPE_FILE;
			return &files[i];
		}
	}
	
	return NULL;
}

static void free_descriptor(File* descriptor) {
	descriptor->driver = NULL;
	descriptor->priv = NULL;
	descriptor->type = FILE_TYPE_NONE;
}

/**
 * Path is absolute path with file name e.g. /boot/kernel.bin
 */
int open(const char* path, int flags, ...) {
	// Name validation check
	int len = strlen(path);
	int ret;
	File* file = NULL;

	char* _flags;
	switch(flags) {
		case O_RDONLY:
			_flags = "r";
			break;
		case O_WRONLY:
			_flags = "w";
			break;
		case O_APPEND:
			_flags = "a";
			break;
		default:
			return -1;
	}

	if((len > FILE_MAX_NAME_LEN - 1) || (len == 0)) {
		ret = -2;
		goto failed;
	}

	file = alloc_descriptor();
	if(!file) {
		ret = -3;
		goto failed;
	}

	// Check whether format of path is valid
	if(path[0] != '/') {
		ret = -4;
		goto failed;
	}

	file->driver = fs_driver(path);
	if(!file->driver) {
		ret = -5;
		goto failed;
	}

	path += strlen(file->driver->path);

	const char* get_file_name(const char* path) {
		char* base = strchr(path, '/');
		return base ? base + 1 : path;
	}

	ret = file->driver->open(file->driver, get_file_name(path), _flags, &file->priv);

	if(ret != FILE_OK)
		goto failed;

	// Return index of file descriptor
	return file - files; 

failed:
	if(file) 
		free_descriptor(file);

	return ret;
}

int close(int fd) {
	File* file = files + fd;

	if(fd < 0 || fd >= FILE_MAX_DESC || file == NULL)
		return -FILE_ERR_BADFD;

	if(file->type != FILE_TYPE_FILE)
		return -FILE_ERR_FTYPE;

	if(file->read_count > 0 || !file->driver->check_sync(file->priv))
		return -FILE_ERR_BUSY;

	if(file->driver->close(file->driver, file->priv) < 0)
		return -2;

	free_descriptor(file);

	return FILE_OK;
}

ssize_t write(int fd, const void* buffer, size_t size) {
	File* file = files + fd;
	
	if(fd < 0 || fd >= FILE_MAX_DESC || file == NULL)
		return -FILE_ERR_BADFD;

	if(file->type != FILE_TYPE_FILE)
		return -FILE_ERR_FTYPE;

	return fs_write(file, (void*)buffer, size);
}

ssize_t read(int fd, void* buffer, size_t size) {
	File* file = files + fd;
	if(fd < 0 || fd >= FILE_MAX_DESC || file == NULL)
		return -FILE_ERR_BADFD;

	if(file->type != FILE_TYPE_FILE)
		return -FILE_ERR_FTYPE;

	return fs_read(file, buffer, size);
}

static bool read_callback(List* blocks, int success, void* context) {
	ReadContext* read_context = context;
	
	int len = 0;
	ListIterator iter;
	list_iterator_init(&iter, blocks);
	while(list_iterator_has_next(&iter)) {
		BufferBlock* block = list_iterator_next(&iter);
		memcpy((uint32_t*)((uint8_t*)read_context->buffer + len), block->buffer, block->size);
		list_iterator_remove(&iter);
		len += block->size;
		free(block);
	}

	if(success < 0)
		len = success;

	read_context->file->read_count -= 1;
	read_context->callback(read_context->buffer, len, read_context->context);
	free(read_context);
	
	return true;
}

int read_async(int fd, void* buffer, size_t size, void(*callback)(void* buffer, int size, void* context), void* context) {
	int ret = FILE_OK;
	File* file = files + fd;

	if(fd < 0 || fd >= FILE_MAX_DESC || file == NULL)
		ret = -FILE_ERR_BADFD;

	if(file->type != FILE_TYPE_FILE)
		ret = -FILE_ERR_FTYPE;

	if(buffer == NULL)
		ret = -FILE_ERR_BADBUF;

	if(ret != FILE_OK)
		goto exit;

	ReadContext* read_context = malloc(sizeof(ReadContext));
	read_context->callback = callback;
	read_context->context = context;
	read_context->buffer = buffer;
	read_context->file = file;

	file->read_count += 1;

	ret = fs_read_async(file, size, read_callback, read_context);
	if(ret < 1) {
		// If no buffer space available
		free(read_context);
		file->read_count -= 1;
	}

exit:
	if(ret != FILE_OK) {
		if(callback)
			callback(buffer, ret, context);
	}

	return ret;
}

typedef struct {
	void(*callback)(void* buffer, int size, void* context);
	void*		context;
	File*		file;
} WriteDone;

void write_callback(void* buffer, int size, void* context) {
	if(!context)
		return;

	WriteDone* write_done = context;
	write_done->file->write_lock = 0;

	if(write_done->callback)
		write_done->callback(buffer, size, write_done->context);

	free(write_done);

}

int write_async(int fd, void* buffer, size_t size, void(*callback)(void* buffer, int size, void* context), void* context, void(*sync_callback)(int errno, void* context2), void* context2) {
	int ret = FILE_OK;
	File* file = files + fd;

	if(fd < 0 || fd >= FILE_MAX_DESC || file == NULL)
		ret = -FILE_ERR_BADFD;

	if(file->type != FILE_TYPE_FILE)
		ret = -FILE_ERR_FTYPE;

	if(buffer == NULL)
		ret = -FILE_ERR_BADBUF;

	if(file->write_lock != 0)
		ret = -FILE_ERR_BUSY;

	WriteDone* write_done = malloc(sizeof(WriteDone));
	if(!write_done)
		ret = -FILE_ERR_NOSPC;

	if(ret != FILE_OK)
		goto exit;

	write_done->file = file;
	write_done->callback = callback;
	write_done->context = context;

	file->write_lock = 1;
	ret = fs_write_async(file, buffer, size, write_callback, write_done, sync_callback, context2);

exit:
	if(ret < 1)
		write_callback(buffer, ret, write_done);

	return ret;
}

off_t lseek(int fd, off_t offset, int whence) {
	File* file = files + fd;

	if(fd < 0 || fd >= FILE_MAX_DESC || file == NULL)
		return -FILE_ERR_BADFD;

	if(file->type != FILE_TYPE_FILE)
		return -FILE_ERR_FTYPE;

	return file->driver->lseek(file->driver, file->priv, offset, whence);
}

int opendir(const char* dir_name) {
	// Check whether format of the path is valid
	if(dir_name[0] != '/')
		return -1;

	File* dir = alloc_descriptor();
	if(dir == NULL)
		return -2;

	dir->driver = fs_driver(dir_name);
	if(!dir->driver) {
		free_descriptor(dir);
		return -3;
	}

	dir_name += strlen(dir->driver->path);

	dir->priv = dir->driver->opendir(dir->driver, dir_name);
	if(!dir->priv) 
		return -4;
	
	dir->type = FILE_TYPE_DIR;

	return dir - files;
}

int readdir(int fd, Dirent* dirent) {
	File* dir = files + fd;

	if(fd < 0 || fd >= FILE_MAX_DESC || dir == NULL)
		return -FILE_ERR_BADFD;

	if(dir->type != FILE_TYPE_DIR)
		return -FILE_ERR_FTYPE;

	return dir->driver->readdir(dir->driver, dir->priv, dirent);
}

void rewinddir(File* dir) {
	// Not implemented yet
}

int closedir(int fd) {
	File* dir = files + fd;

	if(fd < 0 || fd >= FILE_MAX_DESC || dir == NULL)
		return -FILE_ERR_BADFD;

	if(dir->type != FILE_TYPE_DIR)
		return -FILE_ERR_FTYPE;

	if(dir->driver->closedir(dir->driver, dir->priv) < 0)
		return -2;

	free_descriptor(dir);

	return FILE_OK;
}

int file_size(int fd) {
	File* file = files + fd;

	if(fd < 0 || fd >= FILE_MAX_DESC || file == NULL)
		return -FILE_ERR_BADFD;

	if(file->type != FILE_TYPE_FILE)
		return -FILE_ERR_FTYPE;

	return file->driver->file_size(file->priv);
}

int stat(const char* pathname, struct stat* buf) {
	//TODO
	memset(buf, 0, sizeof(struct stat));
	return 0;
}
