#include <pspsdk.h>
#include <pspinit.h>
#include <pspkernel.h>
#include <psploadexec_kernel.h>
#include <pspctrl.h>
#include <zlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "inc/common.h"
#include "inc/vlf.h"
#include "inc/kubridge.h"
#include "ARK4_HEADERS.h"
#define vlf_text_items 20

PSP_MODULE_INFO("ARK-4 Toolkit", 0x200, 1, 1);
PSP_MAIN_THREAD_ATTR(0);

int showback_prev = 0;
int showenter_prev = 0;

char path[64];
char *ebootpath;
static u8 big_buf[10485760] __attribute__((aligned(0x40)));
static u32 EBOOT_PSAR = 0;
VlfText titletext = NULL;
VlfText lt = NULL;
VlfText quote = NULL;
VlfText extract = NULL;
VlfPicture titlepicture = NULL;
char *mode = "Main";

VlfShadowedPicture waiticon = NULL;
int bguseflash = 0;

void ErrorReturn(char *fmt, ...) {
	va_list list;
	char msg[256];
	va_start(list, fmt);
	vsprintf(msg, fmt, list);
	va_end(list);
	sceKernelVolatileMemUnlock(0);
	vlfGuiMessageDialog(msg, VLF_MD_TYPE_ERROR|VLF_MD_BUTTONS_NONE);
	return;


}

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


int zipFileExtract(char *archivepath, int archiveoffs, char *filename, char *outputpath) {
    struct SZIPFileHeader data;
	int CHUNK_SIZE = 512;
    char foundfilename[CHUNK_SIZE];
    u8 *cbuffer;
    u8 outbuf[CHUNK_SIZE];  // Output buffer for decompressed data
    int buf_size = CHUNK_SIZE;

    SceUID fd = sceIoOpen(archivepath, PSP_O_RDONLY, 0);
    if (fd < 0) {
		ErrorReturn("could not open %s", archivepath);
        return -1;
    }
    sceIoLseek(fd, archiveoffs, SEEK_SET);

    while (1) {
        sceIoRead(fd, &data, sizeof(struct SZIPFileHeader));
        if (data.Sig[0] != 0x50 || data.Sig[1] != 0x4B || data.Sig[2] != 0x03 || data.Sig[3] != 0x04) {
            sceIoClose(fd);
			ErrorReturn("Cannot find ZIP in PSAR!");
            return -1;
        }

        sceIoRead(fd, foundfilename, data.FilenameLength);
        foundfilename[data.FilenameLength] = '\0';
        if (data.ExtraFieldLength) {
            sceIoLseek(fd, data.ExtraFieldLength, SEEK_CUR);
        }

        if (!strcmp(strlwr(foundfilename), strlwr(filename))) {
            break;
        } else {
            sceIoLseek(fd, data.DataDescriptor.CompressedSize, SEEK_CUR);
        }
    }
	char op[64] = { 0 };
	char *op_slash = strrchr(outputpath, '/');
	int len = op_slash - outputpath;
	strncpy(op, outputpath, len);
	SceUID verify = sceIoDopen(op);
	if(verify >= 0)
		sceIoDclose(verify);
	else
		sceIoMkdir(op, 0777);
    SceUID out_fd = sceIoOpen(outputpath, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (out_fd < 0) {
		ErrorReturn("could not open outputpath %s", outputpath);
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
			ErrorReturn("Failed to alloc memory");
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
		//ErrorReturn("Compressed: %lu\nUncompressed: %lu", data.DataDescriptor.CompressedSize, data.DataDescriptor.UncompressedSize);
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
		//err = inflate(&stream, Z_SYNC_FLUSH);
		//if (err == Z_STREAM_END || err == Z_OK) {
		if (err == Z_STREAM_END) {
			// Write remaining decompressed data
			int bytes_decompressed = buf_size - stream.avail_out;
			if (bytes_decompressed > 0) {
				sceIoWrite(out_fd, outbuf, bytes_decompressed);
			}
		} else if (err != Z_OK && err != Z_BUF_ERROR) {
			ErrorReturn("Inflation Error");
			inflateEnd(&stream);
			free(cbuffer);
			sceIoClose(fd);
			sceIoClose(out_fd);
			return -1;
		}

		if (stream.avail_out == 0) {
			// Output buffer is full, write only the amount of valid data
			sceIoWrite(out_fd, outbuf, buf_size);
			stream.next_out = outbuf;  // Reset output pointer
			stream.avail_out = buf_size;  // Reset available space in output buffer
		}
	}

	// Handle any remaining decompressed data if inflate did not end at Z_STREAM_END
	/*int bytes_decompressed = buf_size - stream.avail_out;
	if (bytes_decompressed > 0) {
    	sceIoWrite(out_fd, outbuf, bytes_decompressed);
	}
	*/
	

	inflateEnd(&stream);
	free(cbuffer);
	sceIoClose(fd);
	sceIoClose(out_fd);
    return data.DataDescriptor.UncompressedSize;
	}
}

char *ark_cipl_files_list[] = {
	"EBOOT.PBP",
	"ipl_update.prx",
	"kbooti_update.prx",
	"kpspident.prx"
};


// BINS are headers due to dataloss
const char *ark_01234_file[] = {
	"ms0:/PSP/SAVEDATA/ARK_01234/ARK4.BIN",
    "ms0:/PSP/SAVEDATA/ARK_01234/ARK.BIN",
    "ms0:/PSP/SAVEDATA/ARK_01234/ARKX.BIN",
    "ms0:/PSP/SAVEDATA/ARK_01234/FLASH0.ARK",
    "ms0:/PSP/SAVEDATA/ARK_01234/H.BIN",
    "ms0:/PSP/SAVEDATA/ARK_01234/ICON0.PNG",
    "ms0:/PSP/SAVEDATA/ARK_01234/IDSREG.PRX",
    "ms0:/PSP/SAVEDATA/ARK_01234/K.BIN",
    "ms0:/PSP/SAVEDATA/ARK_01234/LANG.ARK",
    "ms0:/PSP/SAVEDATA/ARK_01234/MEDIASYN.PRX",
    "ms0:/PSP/SAVEDATA/ARK_01234/PARAM.SFO",
    "ms0:/PSP/SAVEDATA/ARK_01234/POPSMAN.PRX",
    "ms0:/PSP/SAVEDATA/ARK_01234/POPS.PRX",
    "ms0:/PSP/SAVEDATA/ARK_01234/PS1SPU.PRX",
    "ms0:/PSP/SAVEDATA/ARK_01234/RECOVERY.PRX",
    "ms0:/PSP/SAVEDATA/ARK_01234/SAVEDATA.BIN",
    "ms0:/PSP/SAVEDATA/ARK_01234/SETTINGS.TXT",
    "ms0:/PSP/SAVEDATA/ARK_01234/THEME.ARK",
    "ms0:/PSP/SAVEDATA/ARK_01234/UPDATER.TXT",
    "ms0:/PSP/SAVEDATA/ARK_01234/USBDEV.PRX",
    "ms0:/PSP/SAVEDATA/ARK_01234/VBOOT.PBP",
    "ms0:/PSP/SAVEDATA/ARK_01234/VSHMENU.PRX",
    "ms0:/PSP/SAVEDATA/ARK_01234/XBOOT.PBP",
    "ms0:/PSP/SAVEDATA/ARK_01234/XMBCTRL.PRX",
};

typedef struct {
	const char *filename;
	unsigned char *data;
	unsigned int size;
} ARK4_Struct;


char *pkg_list[] = {
	"ARK-4 Extractor",
	"6.61 OFW",
	"ChronoSwitch",
};

int menu_size = (sizeof(pkg_list)/sizeof(pkg_list[0]));


/*
    Callbacks
*/
int exit_callback(int arg1, int arg2, void *common)
{
    sceKernelExitGame();
    return 0;
}
int CallbackThread(SceSize args, void *argp)
{
    int cbid;

    cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();

    return 0;
}


static int thid = 0;
int SetupCallbacks()
{

    thid = sceKernelCreateThread("exit_thread", CallbackThread, 0x11, 0xFA0, 0, 0);

    if(thid >= 0){sceKernelStartThread(thid, 0, 0);}

    return thid;
}

/*
    Menus & VLF
*/
void SetTitle(char *rco, char *name, char *fmt, ...)
{
    va_list list;
    char msg[256];

    va_start(list, fmt);
    vsprintf(msg, fmt, list);
    va_end(list);

    if(titletext != NULL){titletext = vlfGuiRemoveText(titletext);}

    if(titlepicture != NULL){titlepicture = vlfGuiRemovePicture(titlepicture);}

    titletext = vlfGuiAddText(0, 0, msg);
    titlepicture = vlfGuiAddPictureResource(rco, name, 4, -2);

    vlfGuiSetTitleBarEx(titletext, titlepicture, 1, 0, NULL);

    return;
}

int SetBackground()
{
    if(waiticon == NULL){
        int size = 0, btn = GetKeyPress(0);
        if(btn & PSP_CTRL_LTRIGGER){if(bguseflash){bguseflash = 0;}else{bguseflash = 1;}}
        if(btn & PSP_CTRL_RTRIGGER){vlfGuiSetBackgroundSystem(1);return;}

        if(bguseflash == 0){size = zipFileRead(path, EBOOT_PSAR, "System/Background.bmp", big_buf);}

        if(size != 0){
            int rand = Rand(0, size / 6176);
            vlfGuiSetBackgroundFileBuffer(big_buf+(rand*6176), 6176, 1);
            return 1;
        }
        else{
            bguseflash = 1;

            if(FileExists("flash0:/vsh/resource/01-12.bmp")){
                size = GetFileSize("flash0:/vsh/resource/01-12.bmp");
                int size2 = GetFileSize("flash0:/vsh/resource/13-27.bmp");
                int rand = Rand(0, (size + size2) / 6176);

                if(rand < size / 6176){ReadFile("flash0:/vsh/resource/01-12.bmp", (rand * 6176), big_buf, 6176);}
                else{ReadFile("flash0:/vsh/resource/13-27.bmp", ((rand * 6176) - size), big_buf, 6176);}

                vlfGuiSetBackgroundFileBuffer(big_buf, 6176, 1);

                return 1;
            }
            else{
                vlfGuiSetBackgroundSystem(1);
                return 0;
            }
        }
    }
}

void LoadWave()
{
    int size = zipFileRead(path, EBOOT_PSAR, "System/Wave.omg", big_buf);

    if(size > 0)
    {
        ScePspFMatrix4 matrix;
        ScePspFVector3 scale;
        vlfGuiSetModel(big_buf, size);

        scale.x = scale.y = scale.z = 8.5f;
        gumLoadIdentity(&matrix);
        gumScale(&matrix, &scale);
        vlfGuiSetModelWorldMatrix(&matrix);
    }
}

void OnMainMenuSelect(int sel) {
		if(mode == "Main") {
				if(sel == 0) {
					mode = "ARK-4 Setup Files";
					int cont = vlfGuiMessageDialog("Do you want to extract ARK-4 setup files?\nThis will Extract the following: ARK_Loader, ARK_01234, ARK_Full_Installer, ARK_cIPL", VLF_MD_TYPE_NORMAL|VLF_MD_BUTTONS_YESNO|VLF_MD_INITIAL_CURSOR_NO);
					if(cont != 1) {
						mode = "Main";
						ResetScreen(1, 0, sel);
						return;
					}
					else {
						ResetScreen(0, 0, 0);
						vlfGuiRemoveText(lt);
						extract = vlfGuiAddText(120, 120, "Extracting... Please Wait...");
						sceKernelSuspendThread(thid);
						vlfGuiDrawFrame();

						// ARK_01234
						ARK4_Struct files[] = {
							{ ark_01234_file[0], ARK4_header, size_ARK4_header },
							{ ark_01234_file[1], ARK_header, size_ARK_header },
							{ ark_01234_file[2], ARKX_header, size_ARKX_header },
							{ ark_01234_file[3], FLASH0_header, size_FLASH0_header },
							{ ark_01234_file[4], H_header, size_H_header },
							{ ark_01234_file[5], ICON0_header, size_ICON0_header },
							{ ark_01234_file[6], IDSREG_header, size_IDSREG_header },
							{ ark_01234_file[7], K_header, size_K_header },
							{ ark_01234_file[8], LANG_header, size_LANG_header },
							{ ark_01234_file[9], MEDIASYN_header, size_MEDIASYN_header },
							{ ark_01234_file[10], PARAM_header, size_PARAM_header },
							{ ark_01234_file[11], POPSMAN_header, size_POPSMAN_header },
							{ ark_01234_file[12], POPS_header, size_POPS_header },
							{ ark_01234_file[13], PS1SPU_header, size_PS1SPU_header },
							{ ark_01234_file[14], RECOVERY_header, size_RECOVERY_header },
							{ ark_01234_file[15], SAVEDATA_header, size_SAVEDATA_header },
							{ ark_01234_file[16], SETTINGS_header, size_SETTINGS_header },
							{ ark_01234_file[17], THEME_header, size_THEME_header },
							{ ark_01234_file[18], UPDATER_header, size_UPDATER_header },
							{ ark_01234_file[19], USBDEV_header, size_USBDEV_header },
							{ ark_01234_file[20], VBOOT_header, size_VBOOT_header },
							{ ark_01234_file[21], VSHMENU_header, size_VSHMENU_header },
							{ ark_01234_file[22], XBOOT_header, size_XBOOT_header },
							{ ark_01234_file[23], XMBCTRL_header, size_XMBCTRL_header },
						};

						int fd = -1;
						fd = sceIoDopen("ms0:/PSP/SAVEDATA/ARK_01234");
						if(fd<0) 
							sceIoMkdir("ms0:/PSP/SAVEDATA/ARK_01234", 0777);
						else
							sceIoDclose(fd);
						// ARK_01234 files due to inflate causing corruption
						for(int i = 0;i<sizeof(files)/sizeof(files[0]);i++) {
							sceKernelDcacheWritebackInvalidateRange(files[i].data, files[i].size);
							int ark_fd = sceIoOpen(files[i].filename, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
							if(ark_fd<0) {
								ErrorReturn("Failed to open %s!\n", files[i].filename);
								sceKernelExitGame();
								return -1;
							}
							if(sceIoWrite(ark_fd, files[i].data, files[i].size)<0) {
								sceIoClose(ark_fd);
								ErrorReturn("Failed to write %s!\n", files[i].filename);
								sceKernelExitGame();
								return -1;
							}
							sceIoClose(ark_fd);
						}
						// ARK_Full_Installer
						zipFileExtract(path, EBOOT_PSAR, "ARK_Full_Installer/EBOOT.PBP", "ms0:/PSP/GAME/ARK_Full_Installer/EBOOT.PBP");
						
						// ARK_Loader
						ARK4_Struct ark_loader_files[] = {
							{"ms0:/PSP/GAME/ARK_Loader/EBOOT.PBP", ARK_Loader_header, size_ARK_Loader_header},
							{"ms0:/PSP/GAME/ARK_Loader/K.BIN", ARK_K_header, size_ARK_K_header},
						};
						fd = sceIoDopen("ms0:/PSP/GAME/ARK_Loader");
						if(fd<0)
							sceIoMkdir("ms0:/PSP/GAME/ARK_Loader", 0777);
						else
							sceIoDclose(fd);
						for(int i = 0;i<2;i++) {
							fd = sceIoOpen(ark_loader_files[i].filename, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
							sceIoWrite(fd, ark_loader_files[i].data, ark_loader_files[i].size);
							sceIoClose(fd);
						}

						// ARK_cIPL
						ARK4_Struct ark_cipl_files[] = {
							{"ms0:/PSP/GAME/ARK_cIPL/EBOOT.PBP", ARK_cIPL_header, size_ARK_cIPL_header},
							{"ms0:/PSP/GAME/ARK_cIPL/ipl_update.prx", ARK_ipl_update_header, size_ARK_ipl_update_header},
							{"ms0:/PSP/GAME/ARK_cIPL/kbooti_update.prx", ARK_kbooti_update_header, size_ARK_kbooti_update_header},
							{"ms0:/PSP/GAME/ARK_cIPL/kpspident.prx", ARK_kpspident_header, size_ARK_kpspident_header},
						};
						fd = sceIoDopen("ms0:/PSP/GAME/ARK_cIPL");
						if(fd<0)
							sceIoMkdir("ms0:/PSP/GAME/ARK_cIPL", 0777);
						else
							sceIoDclose(fd);
						for(int i = 0;i<(sizeof(ark_cipl_files_list)/sizeof(ark_cipl_files_list[0]));i++) {
							fd = sceIoOpen(ark_cipl_files[i].filename, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
							if(sceIoWrite(fd, ark_cipl_files[i].data, ark_cipl_files[i].size)<0) {
								sceIoClose(fd);
								ErrorReturn("Failed to write %s!\n", ark_cipl_files[i].filename);
								sceKernelExitGame();
								return -1;
							}
							sceIoClose(fd);
						}



						// Extracted
						vlfGuiRemoveText(extract);
						ErrorReturn("%s successfully installed.", mode);
						int autolaunch = vlfGuiMessageDialog("Would you like to auto launch ARK Loader now?", VLF_MD_TYPE_NORMAL|VLF_MD_BUTTONS_YESNO|VLF_MD_INITIAL_CURSOR_NO);
						if(autolaunch == 1) {
							char menupath[] = "ms0:/PSP/GAME/ARK_Loader/EBOOT.PBP";
							if(strstr(path, "ef0")) {
								menupath[0] = 'e';
								menupath[1] = 'f';
							}
							struct SceKernelLoadExecVSHParam param;
							memset(&param, 0, sizeof(param));
							param.size = sizeof(param);
							param.args = strlen(menupath) + 1;
							param.argp = menupath;
							param.key = "game";
						    sctrlKernelLoadExecVSHWithApitype(0x141, menupath, &param);
							sceKernelExitGame();
						}
						else {
							sceKernelResumeThread(thid);
    						lt = vlfGuiAddText(310, 250, "LT change bg color");
							mode = "Main";
							ResetScreen(1, 0, sel);
							return;
						}
					}
				}
				else if(sel == 1) {
					mode = "OFW";
					int model = kuKernelGetModel();
					int cont = -1;
					if(model == 4) {
						cont = vlfGuiMessageDialog("Do you want to extract 6.61 GO OFW to PSP/GAME/UPDATE/ ?", VLF_MD_TYPE_NORMAL|VLF_MD_BUTTONS_YESNO|VLF_MD_INITIAL_CURSOR_NO);
						if(cont == 1) {
							ResetScreen(0, 0, 0);
							vlfGuiRemoveText(lt);
							extract = vlfGuiAddText(120, 120, "Extracting... Please Wait...");
							sceKernelSuspendThread(thid);
							char *outname = malloc(64);
							char *filetodump = malloc(64);
							memset(big_buf, 0, sizeof(big_buf));
							memset(filetodump, 0, sizeof(filetodump));
							memset(outname, 0, sizeof(outname));
							ResetScreen(0, 0, 0);
							vlfGuiDrawFrame();
							strcpy(filetodump, "OFW/GO/GO661.PBP");
							strcpy(outname, "ms0:/PSP/GAME/UPDATE/EBOOT.PBP");
							zipFileExtract(path, EBOOT_PSAR, filetodump, outname);
						    vlfGuiRemoveText(extract);
							ErrorReturn("%s successfully installed. Returning to XMB to prevent memory issues.", mode);
							free(filetodump);
							free(outname);
							sceKernelExitGame();
							return;
						}
						else {
							mode = "Main";
							ResetScreen(1, 0, sel);
							return;
						}
					}
					else {
						cont = vlfGuiMessageDialog("Do you want to extract 6.61 OFW to PSP/GAME/UPDATE/ ?", VLF_MD_TYPE_NORMAL|VLF_MD_BUTTONS_YESNO|VLF_MD_INITIAL_CURSOR_NO);
						if(cont == 1) {
							char *outname = malloc(64);
							char *filetodump = malloc(64);
							memset(big_buf, 0, sizeof(big_buf));
							memset(filetodump, '\0', sizeof(filetodump));
							memset(outname, '\0', sizeof(outname));
							strcpy(filetodump, "OFW/X000/EBOOT.PBP");
							strcpy(outname, "ms0:/PSP/GAME/UPDATE/EBOOT.PBP");
							ResetScreen(0, 0, 0);
							vlfGuiRemoveText(lt);
							extract = vlfGuiAddText(120, 120, "Extracting... Please Wait...");
							sceKernelSuspendThread(thid);
							vlfGuiDrawFrame();
							zipFileExtract(path, EBOOT_PSAR, filetodump, outname);
						    vlfGuiRemoveText(extract);
							ErrorReturn("%s successfully installed. Returning to XMB to prevent memory issues.", mode);
							free(filetodump);
							free(outname);
							sceKernelExitGame();
							return;
						}
						else {
							mode = "Main";
							ResetScreen(1, 0, sel);
							return;
						}

					}
				}
					else if(sel == 2) {
						mode = "ChronoSwitch";
						int cont = vlfGuiMessageDialog("Do you want to install ChronoSwitch?", VLF_MD_TYPE_NORMAL|VLF_MD_BUTTONS_YESNO|VLF_MD_INITIAL_CURSOR_NO);
						if(cont != 1) {
							mode = "Main";
							ResetScreen(1, 0, sel);
							return;
						}
						else {
							ResetScreen(0, 0, 0);
							extract = vlfGuiAddText(120, 120, "Extracting... Please Wait...");
							sceKernelSuspendThread(thid);
							int fd = sceIoDopen("ms0:/PSP/GAME/ChronoSwitch");
							if(fd<0)
								sceIoMkdir("ms0:/PSP/GAME/ChronoSwitch", 0777);
							else
								sceIoDclose(fd);

							fd = sceIoOpen("ms0:/PSP/GAME/ChronoSwitch/EBOOT.PBP", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
							sceIoWrite(fd, chronoswitch_header, size_chronoswitch_header);
							sceIoClose(fd);
						    vlfGuiRemoveText(extract);
							sceKernelResumeThread(thid);
							ErrorReturn("%s successfully installed.", mode);
							mode = "Main";
    						lt = vlfGuiAddText(310, 250, "LT change bg color");
							ResetScreen(1, 0, sel);
							return;
						}
					
					}
				}

    if(waiticon != NULL){waiticon = vlfGuiRemoveShadowedPicture(waiticon);}

	return;


}

void MainMenu(int sel) {

	if(mode == "Main") {
		vlfGuiCentralMenu(menu_size, pkg_list, sel, OnMainMenuSelect, 0, 0);


	}

	vlfGuiSetRectangleFade(0, VLF_TITLEBAR_HEIGHT, 480, 272-VLF_TITLEBAR_HEIGHT, VLF_FADE_MODE_IN, VLF_FADE_SPEED_VERY_FAST, 0, NULL, NULL, 0);
	return;
	
}
void ResetScreen(int showmenu, int showback, int sel)
{
    vlfGuiCancelCentralMenu();
    
    if(showmenu==1){MainMenu(sel);}
}

void setup() {
	time_t start = time(NULL);
	while(time(NULL) - start < 3) {
		vlfGuiDrawFrame();
	}
}

int app_main(int argc, char *args[]) {

	SetupCallbacks();

	sprintf(path, "%s/EBOOT.PBP", ebootpath);
	ReadFile(path, 0, big_buf, PBPHEADERSIZE);
	EBOOT_PSAR = GetEBOOToffsetBuff(big_buf, DATA_PSAR);
	vlfGuiCacheResource("system_plugin");
    vlfGuiCacheResource("system_plugin_fg");

    vlfGuiSetModelSystem();


    vlfGuiSystemSetup(1, 1, 1); 
    SetTitle("sysconf_plugin", "tex_bar_init_icon", "%s v%i.%02i", module_info.modname, module_info.modversion[1], module_info.modversion[0]);
    
    LoadWave();
    SetBackground();
	vlfGuiAddEventHandler(PSP_CTRL_LTRIGGER, 0, SetBackground, NULL);
	ResetScreen(0, 0, 0);
    quote = vlfGuiAddText(120, 120, "I've got all your ARK-4 needz...");
	setup();
	vlfGuiRemoveText(quote);
	
    lt = vlfGuiAddText(310, 250, "LT change bg color");

	//int btn = GetKeyPress(0);
	ResetScreen(1, 0, 0);

	while(1){vlfGuiDrawFrame();}

	return 0;
}

