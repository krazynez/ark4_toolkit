#include <pspsdk.h>
#include <pspinit.h>
#include <pspkernel.h>
#include <pspctrl.h>
#include <zlib.h>
#include <stdarg.h>
#include <stdio.h>

#include "inc/common.h"
#include "inc/vlf.h"
#define vlf_text_items 20

PSP_MODULE_INFO("Krazy Toolkit", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(0);

VlfText vlf_texts[vlf_text_items];
VlfPicture vlf_picture = NULL;
VlfProgressBar vlf_progressbar = NULL;
int showback_prev = 0;
int showenter_prev = 0;

char path[64];
char *ebootpath;
static u8 big_buf[10485760] __attribute__((aligned(0x40)));
static u32 EBOOT_PSAR = 0;
VlfText titletext = NULL;
VlfText triangle = NULL;
VlfPicture titlepicture = NULL;
char *mode = "Main";
int selitem = 0;

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

#define CHUNK_SIZE 8192  // 8KB buffer size

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
	char op[64];
	char *op_slash = strrchr(outputpath, '/');
	int len = op_slash - outputpath;
	strncpy(op, outputpath, len);
	sceIoMkdir(op, 0777);
    SceUID out_fd = sceIoOpen(outputpath, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (out_fd < 0) {
		ErrorReturn("could not open outputpath %s", outputpath);
        //printf("Could not open output file %s\n", outputpath);
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
            //printf("Failed to allocate memory for buffer!\n");
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
				ErrorReturn("Inflation Error");
                //printf("Inflation error: %d\n", err);
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

char *pkg_list[] = {
			"PSP Tool 1.69 (Will take a bit to extract)",
			"UMDRescue",
			"UMDRescue (1.50 kxploit)",
			"Wallpaper Dumper",
			"Theme Dumper",
			"ChronoSwitch",
			"Exit",
};

char *desc[] = {
		"Originally created by raing3, PSP Tool 1.69 is and updated version\nsupporting later version of DC with other helpful additions.\n",
		"Orginally built with spiritfader, This version of UMDRescue will work\non other CFW's but will only dump GAMES due to kernel limitations.\n",
		"This version of UMDRescue will work\non other 1.50 (OFW) and 1.50 Kernel Add-on CFW's.\nThis is used to dump a 1:1 copy of the UMD Disc.\n",
		"Wallpaper Dumper was initally created due to someone on reddit loosing there relative.\nI created this because the phone on their PSP to preserve the image for them.\n",
		"Theme Dumper is pretty much a 1:1 replica of Wallpaper Dumper,\nbut used to dump OFW themes (PTF).\nThough this can be used to dump CTF's as well.\n",
		"Chronoswitch originally created by Davee, then forked and updated by The Zett.\nI forked and updated to add support for GO to check for the update\nvia ef0/ms0 as well all checking for any EBOOT.PBP at all in /GAME/UPDATE",
};

int size = (sizeof(pkg_list)/sizeof(pkg_list[0]))-1;
int desc_size = (sizeof(desc)/sizeof(desc[0]))-1;


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


void OnMainMenuSelectDesc() {
	int sel = vlfGuiCentralMenuSelection();
	vlfGuiRemoveText(triangle);
	ResetScreen(0, 0, 0);
	if (waiticon == NULL) {
		ErrorReturn(desc[sel]);
		mode = "Main";
		ResetScreen(1, 0, sel);
		return;
	}
}


void OnMainMenuSelect(int sel) {
		selitem = sel;
		if(mode == "Main") {
				if(sel == 0) {
					mode = "PSP Tool";
					int cont = vlfGuiMessageDialog("Do you want to install PSP Tool?", VLF_MD_TYPE_NORMAL|VLF_MD_BUTTONS_YESNO|VLF_MD_INITIAL_CURSOR_NO);
					if(cont != 1) {
						mode = "Main";
						ResetScreen(1, 0, sel);
						return;
					}
					else {
						memset(big_buf, 0, sizeof(big_buf));
						char outname[128];
						char filetodump[] = "psptool/EBOOT.PBP";
						sprintf(outname, "ms0:/PSP/GAME/PSP_Tool/EBOOT.PBP");
						zipFileExtract(path, EBOOT_PSAR, filetodump, outname);
						ErrorReturn("%s successfully installed.", mode);
						mode = "Main";
						ResetScreen(1, 0, sel);
						return;
					}
				}
				else if(sel == 1) {
					mode = "UMDRescue";
					int cont = vlfGuiMessageDialog("Do you want to install UMDRescue?", VLF_MD_TYPE_NORMAL|VLF_MD_BUTTONS_YESNO|VLF_MD_INITIAL_CURSOR_NO);
					if(cont != 1) {
						mode = "Main";
						ResetScreen(1, 0, sel);
						return;
					}
					else {
						memset(big_buf, 0, sizeof(big_buf));
						char outname[128];
						char filetodump[] = "UMDRescue/GAME/EBOOT.PBP";
						sprintf(outname, "ms0:/PSP/GAME/UMDRescue/EBOOT.PBP");
						zipFileExtract(path, EBOOT_PSAR, filetodump, outname);
						ErrorReturn("%s successfully installed.", mode);
						mode = "Main";
						ResetScreen(1, 0, sel);
						return;
					}
				}
				else if(sel == 2) {
					mode = "UMDRescue ( 1.50 kxploit )";
					int cont = vlfGuiMessageDialog("Do you want to install UMDRescue? (1.50 kxploit)", VLF_MD_TYPE_NORMAL|VLF_MD_BUTTONS_YESNO|VLF_MD_INITIAL_CURSOR_NO);
					if(cont != 1) {
						mode = "Main";
						ResetScreen(1, 0, sel);
						return;
					}
					else {
						// Part 1
						memset(big_buf, 0, sizeof(big_buf));
						char outname[128];
						char filetodump[] = "UMDRescue/GAME150/%__SCE__UMDRescue/EBOOT.PBP";
						char mkdir_file[] = "ms0:/PSP/GAME/%__SCE__UMDRescue";
						sceIoMkdir(mkdir_file, 0777);
						sprintf(outname, "ms0:/PSP/GAME/%%__SCE__UMDRescue/EBOOT.PBP");
						strcat(mkdir_file, "/EBOOT.PBP");
						zipFileExtract(path, EBOOT_PSAR, filetodump, mkdir_file);

						// Part 2
						char outname2[64];
						char filetodump2[] ="UMDRescue/GAME150/__SCE__UMDRescue/EBOOT.PBP";
						snprintf(outname2, 41,"ms0:/PSP/GAME/__SCE__UMDRescue/EBOOT.PBP");
						zipFileExtract(path, EBOOT_PSAR, filetodump2, outname2);

						ErrorReturn("%s successfully installed.", mode);
						mode = "Main";
						ResetScreen(1, 0, sel);
						return;
					}
				}
				else if(sel == 3) {
					mode = "Wallpaper Dumper";
					int cont = vlfGuiMessageDialog("Do you want to install Wallpaper Dumper?", VLF_MD_TYPE_NORMAL|VLF_MD_BUTTONS_YESNO|VLF_MD_INITIAL_CURSOR_NO);
					if(cont != 1) {
						mode = "Main";
						ResetScreen(1, 0, sel);
						return;
					}
					else {
						memset(big_buf, 0, sizeof(big_buf));
						char outname[128];
						char filetodump[] = "wallpaper_dumper/EBOOT.PBP";
						sprintf(outname, "ms0:/PSP/GAME/wallpaper_dumper/EBOOT.PBP");
						zipFileExtract(path, EBOOT_PSAR, filetodump, outname);
						ErrorReturn("%s successfully installed.", mode);
						mode = "Main";
						ResetScreen(1, 0, sel);
						return;
					}
				}
				else if(sel == 4) {
					mode = "Theme Dumper";
					int cont = vlfGuiMessageDialog("Do you want to install Wallpaper Dumper?", VLF_MD_TYPE_NORMAL|VLF_MD_BUTTONS_YESNO|VLF_MD_INITIAL_CURSOR_NO);
					if(cont != 1) {
						mode = "Main";
						ResetScreen(1, 0, sel);
						return;
					}
					else {
						memset(big_buf, 0, sizeof(big_buf));
						char outname[128];
						char filetodump[] = "theme_dumper/EBOOT.PBP";
						sprintf(outname, "ms0:/PSP/GAME/theme_dumper/EBOOT.PBP");
						zipFileExtract(path, EBOOT_PSAR, filetodump, outname);
						ErrorReturn("%s successfully installed.", mode);
						mode = "Main";
						ResetScreen(1, 0, sel);
						return;
					}
				}
				else if(sel == 5) {
					mode = "ChronoSwitch";
					int cont = vlfGuiMessageDialog("Do you want to install ChronoSwitch?", VLF_MD_TYPE_NORMAL|VLF_MD_BUTTONS_YESNO|VLF_MD_INITIAL_CURSOR_NO);
					if(cont != 1) {
						mode = "Main";
						ResetScreen(1, 0, sel);
						return;
					}
					else {
						memset(big_buf, 0, sizeof(big_buf));
						char outname[128];
						char filetodump[] = "chronoswitch/EBOOT.PBP";
						sprintf(outname, "ms0:/PSP/GAME/ChronoSwitch/EBOOT.PBP");
						zipFileExtract(path, EBOOT_PSAR, filetodump, outname);
						ErrorReturn("%s successfully installed.", mode);
						mode = "Main";
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
		vlfGuiCentralMenu(size, pkg_list, sel, OnMainMenuSelect, 0, 0);
		
    	/*va_start(list, fmt);
    	vsprintf(msg, fmt, list);
    	va_end(list);
		*/
		vlfGuiChangeCharacterByButton('*', VLF_TRIANGLE);
    	triangle = vlfGuiAddText(20, 250, "* for Description");
		//vlfGuiSetTextXY(triangle, 3, 10);
		//vlfGuiAddText(20, 250, "Z for description");
    	/*int ret = vlfGuiAddEventHandler(PSP_CTRL_TRIANGLE, 0, OnMainMenuSelectDesc(sel), NULL);
		if(ret) {
			mode = "Main";
			ResetScreen(1, 0, sel);
			return;
		}
		*/
	}

	//int btn = GetKeyPress(0);
	//if(btn & PSP_CTRL_TRIANGLE)
	vlfGuiSetRectangleFade(0, VLF_TITLEBAR_HEIGHT, 480, 272-VLF_TITLEBAR_HEIGHT, VLF_FADE_MODE_IN, VLF_FADE_SPEED_VERY_FAST, 0, NULL, NULL, 0);
	return;
	
}
void ResetScreen(int showmenu, int showback, int sel)
{
    int i;

    for(i = 0; i < vlf_text_items; i++){if(vlf_texts[i] != NULL){vlf_texts[i] = vlfGuiRemoveText(vlf_texts[i]);}}
    if(vlf_picture != NULL){vlf_picture = vlfGuiRemovePicture(vlf_picture);}
    if(vlf_progressbar != NULL){vlf_progressbar = vlfGuiRemoveProgressBar(vlf_progressbar);}
    vlfGuiCancelCentralMenu();
    if((vlfGuiGetButtonConfig() && showback_prev) || (!vlfGuiGetButtonConfig() && showenter_prev)){vlfGuiCancelBottomDialog();showback_prev = 0;showenter_prev = 0;} 
    
	selitem = sel;
    if(showmenu==1){MainMenu(sel);}
    //if(showmenu==2){OnMainMenuSelectDesc(sel);}
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
    vlfGuiAddEventHandler(PSP_CTRL_SQUARE, 0, SetBackground, NULL);
    vlfGuiAddEventHandler(PSP_CTRL_TRIANGLE, 0, OnMainMenuSelectDesc, NULL);

	//int btn = GetKeyPress(0);
	ResetScreen(1, 0, 0);

	while(1){vlfGuiDrawFrame();}

	return 0;
}

