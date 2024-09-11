#include <pspsdk.h>
#include <pspinit.h>
#include <pspkernel.h>
#include <pspctrl.h>
#include <zlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

#include "inc/common.h"
#include "inc/vlf.h"
#include "inc/kubridge.h"
#include "ARK4_HEADERS.h"
#define vlf_text_items 20

PSP_MODULE_INFO("ARK-4 Toolkit", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(0);

//VlfText vlf_texts[vlf_text_items];
//VlfPicture vlf_picture = NULL;
//VlfProgressBar vlf_progressbar = NULL;
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

#define CHUNK_SIZE 512  // 8KB buffer size

int zipFileExtract(char *archivepath, int archiveoffs, char *filename, char *outputpath) {
    struct SZIPFileHeader data;
    char foundfilename[1024];
    u8 *cbuffer;
    u8 outbuf[CHUNK_SIZE];  // Output buffer for decompressed data
    int buf_size = CHUNK_SIZE;

    SceUID fd = sceIoOpen(archivepath, PSP_O_RDONLY, 0);
    if (fd < 0) {
		ErrorReturn("could not open %s", archivepath);
        //printf("Could not open %s\n", archivepath);
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
	char op[64] = { 0 };
	char *op_slash = strrchr(outputpath, '/');
	int len = op_slash - outputpath;
	strncpy(op, outputpath, len);
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
			// Write remaining decompressed data
			int bytes_decompressed = buf_size - stream.avail_out;
			if (bytes_decompressed > 0) {
				sceIoWrite(out_fd, outbuf, bytes_decompressed);
			}
			break; // Finished decompression
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
			int bytes_decompressed = buf_size;
			sceIoWrite(out_fd, outbuf, bytes_decompressed);
			stream.next_out = outbuf;  // Reset output pointer
			stream.avail_out = buf_size;  // Reset available space in output buffer
		}
	}

	// Handle any remaining decompressed data if inflate did not end at Z_STREAM_END
	int bytes_decompressed = buf_size - stream.avail_out;
	if (bytes_decompressed > 0) {
    	sceIoWrite(out_fd, outbuf, bytes_decompressed);
	}

	inflateEnd(&stream);
	free(cbuffer);
	sceIoClose(fd);
	sceIoClose(out_fd);
	


      	return data.DataDescriptor.UncompressedSize;
	}
}

char *ark_cipl_files[] = {
	"EBOOT.PBP",
	"ipl_update.prx",
	"kbooti_update.prx",
	"kpspident.prx"
};


// BINS are headers due to dataloss
char *ark_01234_file[] = {
	"FLASH0.ARK",
	"ICON0.PNG",
	"IDSREG.PRX",
	"LANG.ARK",
	"MEDIASYN.PRX",
	"PARAM.SFO",
	"POPSMAN.PRX",
	"POPS.PRX",
	"PS1SPU.PRX",
	"RECOVERY.PRX",
	"SETTINGS.TXT",
	"THEME.ARK",
	"UPDATER.TXT",
	"USBDEV.PRX",
	"VBOOT.PBP",
	"VSHMENU.PRX",
	"XBOOT.PBP",
	"XMBCTRL.PRX",
};

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

int SetupCallbacks()
{
    int thid = 0;

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


//static char outname[64] = { 0 };
//static char filetodump[64] = { 0 };

char out[64] = { 0 };
char fn[64] = { 0 };
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
						memset(big_buf, 0, sizeof(big_buf));
						ResetScreen(0, 0, 0);
						extract = vlfGuiAddText(120, 120, "Extracting... Please Wait...");
						vlfGuiDrawFrame();
						// ARK_cIPL
						char *outname = malloc(64);
						char *filetodump = malloc(64);
						for(int i=0;i<sizeof(ark_cipl_files)/sizeof(ark_cipl_files[0]);i++) {
							memset(filetodump, '\0', sizeof(filetodump));
							memset(outname, '\0', sizeof(outname));
							sprintf(filetodump, "ARK_cIPL/%s", ark_cipl_files[i]);
							sprintf(outname, "ms0:/PSP/GAME/ARK_cIPL/%s", ark_cipl_files[i]);
							zipFileExtract(path, EBOOT_PSAR, filetodump, outname);
						}

						// ARK_Loader
						memset(filetodump, '\0', sizeof(filetodump));
						memset(outname, '\0', sizeof(outname));
						strcpy(filetodump, "ARK_Loader/EBOOT.PBP");
						strcpy(outname, "ms0:/PSP/GAME/ARK_Loader/EBOOT.PBP");
						zipFileExtract(path, EBOOT_PSAR, filetodump, outname);
						memset(filetodump, '\0', sizeof(filetodump));
						memset(outname, '\0', sizeof(outname));
						strcpy(filetodump, "ARK_Loader/K.BIN");
						strcpy(outname, "ms0:/PSP/GAME/ARK_Loader/K.BIN");
						zipFileExtract(path, EBOOT_PSAR, filetodump, outname);

						// ARK_01234
						for(int j=0;j<sizeof(ark_01234_file)/sizeof(ark_01234_file[0]);j++) {
							memset(filetodump, '\0', sizeof(filetodump));
							memset(outname, '\0', sizeof(outname));
							sprintf(filetodump, "ARK_01234/%s", ark_01234_file[j]);
							sprintf(outname, "ms0:/PSP/SAVEDATA/ARK_01234/%s", ark_01234_file[j]);
							zipFileExtract(path, EBOOT_PSAR, filetodump, outname);
						}

						
						int fd;
						fd = sceIoOpen("ms0:/PSP/SAVEDATA/ARK_01234/K.BIN", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
						sceIoWrite(fd, K_header, size_K_header);
						sceIoClose(fd);
						fd = sceIoOpen("ms0:/PSP/SAVEDATA/ARK_01234/H.BIN", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
						sceIoWrite(fd, H_header, size_H_header);
						sceIoClose(fd);
						fd = sceIoOpen("ms0:/PSP/SAVEDATA/ARK_01234/ARK.BIN", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
						sceIoWrite(fd, ARK_header, size_ARK_header);
						sceIoClose(fd);
						fd = sceIoOpen("ms0:/PSP/SAVEDATA/ARK_01234/ARK4.BIN", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
						sceIoWrite(fd, ARK4_header, size_ARK4_header);
						sceIoClose(fd);
						fd = sceIoOpen("ms0:/PSP/SAVEDATA/ARK_01234/ARKX.BIN", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
						sceIoWrite(fd, ARKX_header, size_ARKX_header);
						sceIoClose(fd);
						fd = sceIoOpen("ms0:/PSP/SAVEDATA/ARK_01234/SAVEDATA.BIN", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
						sceIoWrite(fd, SAVEDATA_header, size_SAVEDATA_header);
						sceIoClose(fd);

						// ARK_Full_Installer
						memset(filetodump, '\0', sizeof(filetodump));
						memset(outname, '\0', sizeof(outname));
						strcpy(filetodump, "ARK_Full_Installer/EBOOT.PBP");
						strcpy(outname, "ms0:/PSP/GAME/ARK_Full_Installer/EBOOT.PBP");
						zipFileExtract(path, EBOOT_PSAR, filetodump, outname);


						free(filetodump);
						free(outname);


						// Extracted
						vlfGuiRemoveText(extract);
						ErrorReturn("%s successfully installed.", mode);
						mode = "Main";
						ResetScreen(1, 0, sel);
						return;
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
							extract = vlfGuiAddText(120, 120, "Extracting... Please Wait...");
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
							ErrorReturn("%s successfully installed.", mode);
							free(filetodump);
							free(outname);
							mode = "Main";
							ResetScreen(1, 0, sel);
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
							extract = vlfGuiAddText(120, 120, "Extracting... Please Wait...");
							vlfGuiDrawFrame();
							zipFileExtract(path, EBOOT_PSAR, filetodump, outname);
						    vlfGuiRemoveText(extract);
							ErrorReturn("%s successfully installed.", mode);
							mode = "Main";
							free(filetodump);
							free(outname);
							ResetScreen(1, 0, sel);
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
							char *outname = malloc(64);
							char *filetodump = malloc(64);
							memset(big_buf, 0, sizeof(big_buf));
							memset(filetodump, 0, sizeof(filetodump));
							memset(outname, 0, sizeof(outname));
							strcpy(filetodump, "chronoswitch/EBOOT.PBP");
							strcpy(outname, "ms0:/PSP/GAME/ChronoSwitch/EBOOT.PBP");
							zipFileExtract(path, EBOOT_PSAR, filetodump, outname);
							ErrorReturn("%s successfully installed.", mode);
							mode = "Main";
							free(filetodump);
							free(outname);
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
    //int i;

    //for(i = 0; i < vlf_text_items; i++){if(vlf_texts[i] != NULL){vlf_texts[i] = vlfGuiRemoveText(vlf_texts[i]);}}
    //if(vlf_picture != NULL){vlf_picture = vlfGuiRemovePicture(vlf_picture);}
    //if(vlf_progressbar != NULL){vlf_progressbar = vlfGuiRemoveProgressBar(vlf_progressbar);}
    vlfGuiCancelCentralMenu();
    //if((vlfGuiGetButtonConfig() && showback_prev) || (!vlfGuiGetButtonConfig() && showenter_prev)){vlfGuiCancelBottomDialog();showback_prev = 0;showenter_prev = 0;} 
    
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

