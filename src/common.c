#include <pspsdk.h>
#include <pspkernel.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <zlib.h>
#include <pspctrl.h>
#include <common.h>

/*
	File/Directory
*/
int ReadFile(char *file, int seek, char *buf, int size)
{
	SceUID fd = sceIoOpen(file, PSP_O_RDONLY, 0);
	if(fd < 0){return fd;}

	if(seek > 0){
		if(sceIoLseek(fd, seek, PSP_SEEK_SET) != seek){
			sceIoClose(fd);
			return -1;
		}
	}

	int read = sceIoRead(fd, buf, size);
	
	sceIoClose(fd);
	return read;
}

int WriteFile(char *file, int seek, char *buf, int size)
{
	int i, pathlen = 0;
	char dirpath[128];

	for(i=1; i<(strlen(file)); i++){
		if(strncmp(file+i-1, "/", 1) == 0){
			pathlen=i-1;
			strncpy(dirpath, file, pathlen);
			dirpath[pathlen] = 0;
			sceIoMkdir(dirpath, 0777);
		}
	}

	if(FileExists(file)){sceIoRemove(file);}

	SceUID fd = sceIoOpen(file, PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC, 0777);
	if(fd < 0){return fd;}

	if(seek > 0){
		if(sceIoLseek(fd, seek, PSP_SEEK_SET) != seek){
			sceIoClose(fd);
			return -1;
		}
	}

	int written = sceIoWrite(fd, buf, size);

	sceIoClose(fd);
	return written;
}

int FileExists(char *file)
{
	if(access(file, F_OK)){return 0;}
	else{return 1;}
}
int GetFileSize(char *file)
{
	SceIoStat info;
	memset(&info, 0, sizeof(info));
	sceIoGetstat(file, &info);
	return info.st_size;
}
int DirExists(char *dir)
{
	SceUID d = sceIoDopen(dir);
	if(d < 0){return 0;}
	sceIoClose(d);

	return 1;
}
/*
	ZIP
*/

int zipFileRead(char *archivepath, int archiveoffs, char *filename, char *buf)
{
	struct SZIPFileHeader data;
	char foundfilename[1024];
	u8 *cbuffer;

	SceUID fd = sceIoOpen(archivepath, PSP_O_RDONLY, 0);
	sceIoLseek(fd, archiveoffs, SEEK_CUR);
	
	while(1){
		sceIoRead(fd, &data, sizeof(struct SZIPFileHeader));
		if(data.Sig[0] != 0x50 || data.Sig[1] != 0x4B || data.Sig[2] != 0x03 || data.Sig[3] != 0x04){sceIoClose(fd);return NULL;} // check correct sig

		sceIoRead(fd, foundfilename, data.FilenameLength); // get filename
		foundfilename[data.FilenameLength] = 0;
		if(data.ExtraFieldLength){sceIoLseek(fd, data.ExtraFieldLength, SEEK_CUR);} // seek to the data start

		if(!strcmp(strlwr(foundfilename), strlwr(filename))){break;}
		else{sceIoLseek(fd, data.DataDescriptor.CompressedSize, SEEK_CUR);} // seek to the end of the file
	}

	if(data.CompressionMethod == 0){
		sceIoRead(fd, buf, data.DataDescriptor.UncompressedSize);
	}
	else if(data.CompressionMethod == 8){
		int inflatedbytes = 0, readbytes = 0;
		z_stream stream;
        u32 err;

        cbuffer = malloc(131072);

		if(cbuffer == NULL){sceIoClose(fd);return NULL;}

        stream.next_in = (Bytef*)cbuffer;
        stream.avail_in = 131072;
		
        stream.next_out = (Bytef*)buf;
        stream.avail_out = data.DataDescriptor.UncompressedSize;

        stream.zalloc = Z_NULL;
        stream.zfree  = Z_NULL;
        stream.opaque = Z_NULL;

        err = inflateInit2(&stream, -MAX_WBITS);

		if(data.DataDescriptor.CompressedSize > 131072){readbytes += sceIoRead(fd, cbuffer, 131072);}
		else{readbytes += sceIoRead(fd, cbuffer, data.DataDescriptor.CompressedSize);}

		if(err == Z_OK){
 			while(inflatedbytes < data.DataDescriptor.CompressedSize){
				err = inflate(&stream, Z_SYNC_FLUSH);
				inflatedbytes = readbytes;

				if(err == Z_BUF_ERROR){
					stream.next_in = (Bytef*)cbuffer;
					stream.avail_in = 131072;

					if(data.DataDescriptor.CompressedSize > readbytes + 131072){readbytes += sceIoRead(fd, cbuffer, 131072);}
					else{readbytes += sceIoRead(fd, cbuffer, data.DataDescriptor.CompressedSize - readbytes);}
				}
			}
			err = Z_OK;
			inflateEnd(&stream);
		}

		free(cbuffer);
	}

	sceIoClose(fd);

	return data.DataDescriptor.UncompressedSize;
}

int zipFileExtract(char *archivepath, int archiveoffs, char *filename, char *outname, char *buf)
{
	int size = zipFileRead(archivepath, archiveoffs, filename, buf);
	if(size == NULL){return NULL;}

	printf("\n\nMade it past!\n\n");
	size = WriteFile(outname, 0, buf, size);  

	return size;
}

int zipFileExists(char *archivepath, int archiveoffs, char *filename)
{
	struct SZIPFileHeader data;
	char foundfilename[1024];

	SceUID fd = sceIoOpen(archivepath, PSP_O_RDONLY, 0);
	sceIoLseek(fd, archiveoffs, SEEK_CUR);
	
	while(1){
		sceIoRead(fd, &data, sizeof(struct SZIPFileHeader));
		if(data.Sig[0] != 0x50 || data.Sig[1] != 0x4B || data.Sig[2] != 0x03 || data.Sig[3] != 0x04){sceIoClose(fd);return -1;} // check correct sig

		sceIoRead(fd, foundfilename, data.FilenameLength); // get filename
		foundfilename[data.FilenameLength] = 0;
		if(data.ExtraFieldLength){sceIoLseek(fd, data.ExtraFieldLength, SEEK_CUR);} // seek to the data start

		if(!strcmp(strlwr(foundfilename), strlwr(filename))){sceIoClose(fd);return 1;}
		else{sceIoLseek(fd, data.DataDescriptor.CompressedSize, SEEK_CUR);} // seek to the end of the file
	}

	sceIoClose(fd);

	return 0;
}
int zipFileSize(char *archivepath, int archiveoffs, char *filename)
{
	struct SZIPFileHeader data;
	char foundfilename[1024];

	SceUID fd = sceIoOpen(archivepath, PSP_O_RDONLY, 0);
	sceIoLseek(fd, archiveoffs, SEEK_CUR);
	
	while(1){
		sceIoRead(fd, &data, sizeof(struct SZIPFileHeader));
		if(data.Sig[0] != 0x50 || data.Sig[1] != 0x4B || data.Sig[2] != 0x03 || data.Sig[3] != 0x04){sceIoClose(fd);return NULL;} // check correct sig

		sceIoRead(fd, foundfilename, data.FilenameLength); // get filename
		foundfilename[data.FilenameLength] = 0;
		if(data.ExtraFieldLength){sceIoLseek(fd, data.ExtraFieldLength, SEEK_CUR);} // seek to the data start

		if(strcmp(strlwr(foundfilename), strlwr(filename)) == 0){break;}
		else{sceIoLseek(fd, data.DataDescriptor.CompressedSize, SEEK_CUR);} // seek to the end of the file
	}

	sceIoClose(fd);

	return data.DataDescriptor.UncompressedSize;
}
int zipFileSizeCmp(char *archivepath, int archiveoffs, char *filename)
{
	struct SZIPFileHeader data;
	char foundfilename[1024];

	SceUID fd = sceIoOpen(archivepath, PSP_O_RDONLY, 0);
	sceIoLseek(fd, archiveoffs, SEEK_CUR);
	
	while(1){
		sceIoRead(fd, &data, sizeof(struct SZIPFileHeader));
		if(data.Sig[0] != 0x50 || data.Sig[1] != 0x4B || data.Sig[2] != 0x03 || data.Sig[3] != 0x04){sceIoClose(fd);return NULL;} // check correct sig

		sceIoRead(fd, foundfilename, data.FilenameLength); // get filename
		foundfilename[data.FilenameLength] = 0;
		if(data.ExtraFieldLength){sceIoLseek(fd, data.ExtraFieldLength, SEEK_CUR);} // seek to the data start

		if(!strcmp(strlwr(foundfilename), strlwr(filename))){break;}
		else{sceIoLseek(fd, data.DataDescriptor.CompressedSize, SEEK_CUR);} // seek to the end of the file
	}

	sceIoClose(fd);

	return data.DataDescriptor.CompressedSize;
}

int GetEBOOToffset(char *file, u32 filename)
{
	char buf[PBPHEADERSIZE];
	memset(buf, 0, PBPHEADERSIZE);
	ReadFile(file, 0, buf, PBPHEADERSIZE);

	int ret = *(u32 *)&buf[filename];
	return ret;
}
int GetEBOOToffsetBuff(char *buf, u32 filename)
{
	int ret = *(u32 *)&buf[filename];
	return ret;
}
