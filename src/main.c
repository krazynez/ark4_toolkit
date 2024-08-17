#include <pspsdk.h>
#include <pspinit.h>
#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdebug.h>
#include <zlib.h>

#include "common.h"

PSP_MODULE_INFO("Krazy_toolkit", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(0);

#define printf pspDebugScreenPrintf

#define arrow "->"

static u8 big_buf[24117248] __attribute__((aligned(0x40)));

int GetEBOOToffsetBuff(char *buf, u32 filename)
{
	int ret = *(u32 *)&buf[filename];
	return ret;
}
int FileExists(char *file)
{
	if(access(file, F_OK)){return 0;}
	else{return 1;}
}
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

#define CHUNK_SIZE 8192  // 8KB buffer size

int zipFileExtract(char *archivepath, int archiveoffs, char *filename, char *outputpath) {
    struct SZIPFileHeader data;
    char foundfilename[1024];
    u8 *cbuffer;
    u8 outbuf[CHUNK_SIZE];  // Output buffer for decompressed data
    int buf_size = CHUNK_SIZE;

    SceUID fd = sceIoOpen(archivepath, PSP_O_RDONLY, 0);
    if (fd < 0) {
        printf("Could not open %s\n", archivepath);
        return -1;
    }
    sceIoLseek(fd, archiveoffs, SEEK_SET);

    while (1) {
        sceIoRead(fd, &data, sizeof(struct SZIPFileHeader));
        if (data.Sig[0] != 0x50 || data.Sig[1] != 0x4B || data.Sig[2] != 0x03 || data.Sig[3] != 0x04) {
            sceIoClose(fd);
            return -1;
        }

        sceIoRead(fd, foundfilename, data.FilenameLength);
        foundfilename[data.FilenameLength] = 0;
        if (data.ExtraFieldLength) {
            sceIoLseek(fd, data.ExtraFieldLength, SEEK_CUR);
        }

        if (!strcmp(strlwr(foundfilename), strlwr(filename))) {
            break;
        } else {
            sceIoLseek(fd, data.DataDescriptor.CompressedSize, SEEK_CUR);
        }
    }
	char op[64];
	char *op_slash = strrchr(outputpath, '/');
	int len = op_slash - outputpath;
	strncpy(op, outputpath, len);
	sceIoMkdir(op, 0777);
    SceUID out_fd = sceIoOpen(outputpath, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (out_fd < 0) {
        printf("Could not open output file %s\n", outputpath);
        sceIoClose(fd);
        return -1;
    }

    if (data.CompressionMethod == 0) {
        // Uncompressed, just read the data directly
        sceIoRead(fd, outbuf, data.DataDescriptor.UncompressedSize);
        sceIoWrite(out_fd, outbuf, data.DataDescriptor.UncompressedSize);
    } else if (data.CompressionMethod == 8) {
        // Compressed with DEFLATE
        int readbytes = 0;
        z_stream stream;
        u32 err;

        cbuffer = malloc(buf_size);
        if (cbuffer == NULL) {
            printf("Failed to allocate memory for buffer!\n");
            sceIoClose(fd);
            sceIoClose(out_fd);
            return -1;
        }

        stream.zalloc = Z_NULL;
        stream.zfree = Z_NULL;
        stream.opaque = Z_NULL;
        stream.avail_in = 0;
        stream.next_in = Z_NULL;
        stream.avail_out = buf_size;
        stream.next_out = outbuf;

        err = inflateInit2(&stream, -MAX_WBITS);
        if (err != Z_OK) {
            free(cbuffer);
            sceIoClose(fd);
            sceIoClose(out_fd);
            return -1;
        }

        while (readbytes < data.DataDescriptor.CompressedSize) {
            if (stream.avail_in == 0) {
                int bytes_to_read = buf_size;
                if (data.DataDescriptor.CompressedSize - readbytes < buf_size) {
                    bytes_to_read = data.DataDescriptor.CompressedSize - readbytes;
                }
                stream.avail_in = sceIoRead(fd, cbuffer, bytes_to_read);
                stream.next_in = cbuffer;
                readbytes += stream.avail_in;
            }

            err = inflate(&stream, Z_NO_FLUSH);
            if (err == Z_STREAM_END) {
                // Write any remaining decompressed data
                sceIoWrite(out_fd, outbuf, buf_size - stream.avail_out);
                break; // Finished decompression
            } else if (err != Z_OK && err != Z_BUF_ERROR) {
                printf("Inflation error: %d\n", err);
                inflateEnd(&stream);
                free(cbuffer);
                sceIoClose(fd);
                sceIoClose(out_fd);
                return -1;
            }

            if (stream.avail_out == 0) {
                // Output buffer is full, write it to the output file
                sceIoWrite(out_fd, outbuf, buf_size);
                stream.next_out = outbuf;  // Reset output pointer
                stream.avail_out = buf_size;  // Reset available space in output buffer
            }
        }

        inflateEnd(&stream);
        free(cbuffer);
    }

    sceIoClose(fd);
    sceIoClose(out_fd);
    return data.DataDescriptor.UncompressedSize;
}

/*int zipFileExtract(char *archivepath, int archiveoffs, char *filename, char *outname, char *buf)
{
	//int zipFileRead(char *archivepath, int archiveoffs, char *filename, char *outputpath) {
	int size = zipFileRead(archivepath, archiveoffs, filename, outname);
	if(size == NULL){return NULL;}

	//size = WriteFile(outname, 0, buf, size);  

	return size;
}
*/

char *list[] = {
			"PSP Tool 1.69 (Will take a bit to extract)\n",
			"UMDRescue\n",
			"UMDRescue (1.50 kxploit)\n",
			"Wallpaper Dumper\n",
			"Theme Dumper\n",
			"Exit\n",
};

char *desc[] = {
		"Originally created by raing3, PSP Tool 1.69 is and updated version\nsupporting later version of DC with other helpful additions.\n",
		"Orginally built with spiritfader, This version of UMDRescue will work\non other CFW's but will only dump GAMES due to kernel limitations.\n",
		"This version of UMDRescue will work\non other 1.50 (OFW) and 1.50 Kernel Add-on CFW's.\nThis is used to dump a 1:1 copy of the UMD Disc.\n",
		"Wallpaper Dumper was initally created due to someone on reddit loosing there relative.\nI created this because the phone on their PSP to preserve the image for them.\n",
		"Theme Dumper is pretty much a 1:1 replica of Wallpaper Dumper,\nbut used to dump OFW themes (PTF).\nThough this can be used to dump CTF's as well.\n",
};

int size = (sizeof(list)/sizeof(list[0]))-1;

int main(int argc, char *args[]) {

	char path[64] = { 0 };
	strncpy(path, args[0], strlen(args)+10);
	strcat(path, "krazy_toolkit/EBOOT.PBP");
	ReadFile(path, 0, big_buf, PBPHEADERSIZE);
	u32 EBOOT_PSAR = GetEBOOToffsetBuff(big_buf, DATA_PSAR);

	pspDebugScreenInit();
	SceCtrlData pad;
	int j = 0;
	pspDebugScreenSetTextColor(0xAA00FF00);
	while(1) {
		pspDebugScreenSetXY(0,0);
		sceDisplayWaitVblankStart();
		printf("\n Krazynez's Toolkit\n --- I've got all your PSP needs ---\n\n");
		sceCtrlReadBufferPositive(&pad, 1);
		for(int i = 0; i < size+1; i++) {

			if(j==i) {
				printf("%s %s", arrow, list[i]);	
			}
			else {
				printf("%3s%s", " ", list[i]);	
			}
		}
		if(j==0 && (pad.Buttons == (PSP_CTRL_CROSS))) {
			memset(big_buf, 0, sizeof(big_buf));
			char outname[128];
			char filetodump[] = "psptool/EBOOT.PBP";
			sprintf(outname, "ms0:/PSP/GAME/PSP_Tool/EBOOT.PBP");
			zipFileExtract(path, EBOOT_PSAR, filetodump, outname);
			sceKernelExitGame();
		}
		else if(j==1 && (pad.Buttons == (PSP_CTRL_CROSS))) {
			memset(big_buf, 0, sizeof(big_buf));
			char outname[128];
			char filetodump[] = "UMDRescue/GAME/EBOOT.PBP";
			sprintf(outname, "ms0:/PSP/GAME/UMDRescue/EBOOT.PBP");
			zipFileExtract(path, EBOOT_PSAR, filetodump, outname);
			sceKernelExitGame();
		}
		else if(j==2 && (pad.Buttons == (PSP_CTRL_CROSS))) {

			// Part 1
			memset(big_buf, 0, sizeof(big_buf));
			char outname[64];
			char filetodump[] = "UMDRescue/GAME150/%__SCE__UMDRescue/EBOOT.PBP";
			char mkdir_file[] = "ms0:/PSP/GAME/%__SCE__UMDRescue";
			sceIoMkdir(mkdir_file, 0777);
			sprintf(outname, "ms0:/PSP/GAME/%%__SCE__UMDRescue/EBOOT.PBP");
			strcat(mkdir_file, "/EBOOT.PBP");
			zipFileExtract(path, EBOOT_PSAR, filetodump, mkdir_file);
			
			// Part 2
			char outname2[64];
			//memset(big_buf, 0, sizeof(big_buf));
			//memset(outname2, 0, sizeof(outname2));
			char filetodump2[] ="UMDRescue/GAME150/__SCE__UMDRescue/EBOOT.PBP";
			snprintf(outname2, 41,"ms0:/PSP/GAME/__SCE__UMDRescue/EBOOT.PBP");
			zipFileExtract(path, EBOOT_PSAR, filetodump2, outname2);

			sceKernelExitGame();
		}
		else if(j==3 && (pad.Buttons == (PSP_CTRL_CROSS))) {
			//memset(big_buf, 0, sizeof(big_buf));
			char outname[128];
			char filetodump[] = "wallpaper_dumper/EBOOT.PBP";
			sprintf(outname, "ms0:/PSP/GAME/wallpaper_dumper/EBOOT.PBP");
			zipFileExtract(path, EBOOT_PSAR, filetodump, outname);
			sceKernelExitGame();
		}
		else if(j==4 && (pad.Buttons == (PSP_CTRL_CROSS))) {
			//memset(big_buf, 0, sizeof(big_buf));
			char outname[128];
			char filetodump[] = "theme_dumper/EBOOT.PBP";
			sprintf(outname, "ms0:/PSP/GAME/theme_dumper/EBOOT.PBP");
			zipFileExtract(path, EBOOT_PSAR, filetodump, outname);
			sceKernelExitGame();
		}
		else if(j==size && (pad.Buttons == (PSP_CTRL_CROSS)))
			sceKernelExitGame();
		if(j>size) j = 0;
		if(j<0) j = size;
		if((pad.Buttons & PSP_CTRL_DOWN) == PSP_CTRL_DOWN) {
			pspDebugScreenClear();
			j++;
		}
		if((pad.Buttons & PSP_CTRL_UP) == PSP_CTRL_UP) {
			pspDebugScreenClear();
			j--;
		}
		if(pad.Buttons == PSP_CTRL_TRIANGLE && j != size) {
			pspDebugScreenClear();
			printf(desc[j]);
			pspDebugScreenSetXY(25,30);
			printf("( X ) to go back");
			while(1) {
				sceCtrlReadBufferPositive(&pad, 1);
				if(pad.Buttons == PSP_CTRL_CROSS) {
					pspDebugScreenClear();
					break;
				}
			}
		}

		pspDebugScreenSetXY(20,30);
		printf("( /_\\ ) to show description");
		/*
		if(i>3)i=0;
		if(i<0)i=2;
		if(i==0) {
			//pspDebugScreenClear();
			printf("Krazynez's Toolkit\nFor all your psp needs\n\n");
			printf("%s PSP Tool 1.69\n", arrow);
			printf("UMDRescue\n");
			printf("Exit\n");
		}
		else if(i==1) {
			//pspDebugScreenClear();
			printf("Krazynez's Toolkit\nFor all your psp needs\n\n");
			printf("PSP Tool 1.69\n");
			printf("%s UMDRescue\n", arrow);
			printf("Exit\n");
		}
		else if(i==2) {
			//pspDebugScreenClear();
			printf("Krazynez's Toolkit\nFor all your psp needs\n\n");
			printf("PSP Tool 1.69\n");
			printf("UMDRescue\n");
			printf("%s Exit\n", arrow);
		}

		if(i==2 && (pad.Buttons == PSP_CTRL_CROSS))
			sceKernelExitGame();
			*/
		sceKernelDelayThread(40000);
	}


	return 0;
}

