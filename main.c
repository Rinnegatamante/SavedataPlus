#define _PSP2_KERNEL_CLIB_H_ // Prevent incompatibility between libk and sceLibc
#include <vitasdk.h>
#include <taihen.h>
#include <libk/stdio.h>
#include <libk/stdarg.h>
#include <libk/string.h>
#include <kuio.h>

#define HOOKS_NUM  6      // Hooked functions num
#define BUF_SIZE   2048   // Savedata buffer size

static uint8_t current_hook = 0;
static SceUID hooks[HOOKS_NUM];
static tai_hook_ref_t refs[HOOKS_NUM];
static char buffer[BUF_SIZE];
static char titleid[16];
static char fname[256];

static char log_file[64];
void LOG(char* format, ...){
	char str[512] = { 0 };
	va_list va;

	va_start(va, format);
	vsnprintf(str, 512, format, va);
	va_end(va);
	
	SceUID fd;
	sprintf(log_file, "ux0:/data/%s.log", titleid);
	kuIoOpen(log_file, SCE_O_WRONLY | SCE_O_APPEND | SCE_O_CREAT, &fd);
	kuIoWrite(fd, str, strlen(str));
	kuIoWrite(fd, "\n", 1);
	kuIoClose(fd);
}

void hookFunction(uint32_t nid, const void *func){
	hooks[current_hook] = taiHookFunctionImport(&refs[current_hook], TAI_MAIN_MODULE, TAI_ANY_LIBRARY, nid, func);
	current_hook++;
}

int sceAppUtilSaveDataDataSave_patched(SceAppUtilSaveDataFileSlot* slot, SceAppUtilSaveDataFile* files, unsigned int fileNum, SceAppUtilSaveDataMountPoint* mountPoint, SceSize* requiredSizeKB) {
	LOG("Called sceAppUtilSaveDataDataSave on slot %u for %u files", slot->id, fileNum);
	
	// Writing savedata files
	SceUID fd;
	uint32_t fileIdx = 0;
	while (fileIdx < fileNum){
		sprintf(fname, "ux0:/data/savegames/%s/SLOT%u/%s", titleid, slot->id, files[fileIdx].filePath);
		switch (files[fileIdx].mode){
			case SCE_APPUTIL_SAVEDATA_DATA_SAVE_MODE_FILE:
				kuIoOpen(fname, SCE_O_WRONLY | SCE_O_CREAT, &fd);
				kuIoLseek(fd, files[fileIdx].offset, SEEK_SET);
				int i = 0;
				uint8_t* save_buf = (uint8_t*)files[fileIdx].buf;
				while (i < files->bufSize){
					int len = (i + BUF_SIZE > files[fileIdx].bufSize) ? files[fileIdx].bufSize - i : BUF_SIZE;
					kuIoWrite(fd, &save_buf[i], len);
					i += len;
				}
				kuIoClose(fd);
				break;
			default:
				kuIoMkdir(fname);
				break;
		}
		fileIdx++;
	}
	
	// Writing slot param file
	sprintf(fname, "ux0:/data/savegames/%s/SLOT%u/SlotParam.bin", titleid, slot->id);
	kuIoOpen(fname, SCE_O_WRONLY | SCE_O_CREAT, &fd);
	int i = 0;
	uint8_t* paramBuf = (uint8_t*)slot->slotParam;
	while (i < sizeof(SceAppUtilSaveDataSlotParam)){
		int len = (i + BUF_SIZE > sizeof(SceAppUtilSaveDataSlotParam)) ? sizeof(SceAppUtilSaveDataSlotParam) - i : BUF_SIZE;
		kuIoWrite(fd, &paramBuf[i], len);
		i += len;
	}
	kuIoClose(fd);
	
	return 0;
}

int sceAppUtilSaveDataSlotGetParam_patched(unsigned int slotId, SceAppUtilSaveDataSlotParam* param, SceAppUtilSaveDataMountPoint* mountPoint) {
	LOG("Called sceAppUtilSaveDataSlotGetParam on slot %u", slotId);
	
	// Checking if slot param file exists
	sprintf(fname, "ux0:/data/savegames/%s/SLOT%u/SlotParam.bin", titleid, slotId);
	SceUID fd;
	kuIoOpen(fname, SCE_O_RDONLY, &fd);
	if (fd < 0) return SCE_APPUTIL_ERROR_SAVEDATA_SLOT_NOT_FOUND;
	else{
		
		// Reading slot param info
		int i = 0;
		uint8_t* paramBuf = (uint8_t*)param;
		while (i < sizeof(SceAppUtilSaveDataSlotParam)){
			int len = (i + BUF_SIZE > sizeof(SceAppUtilSaveDataSlotParam)) ? sizeof(SceAppUtilSaveDataSlotParam) - i : BUF_SIZE;
			kuIoRead(fd, &paramBuf[i], len);
			i += len;
		}
		kuIoClose(fd);
		
	}
	return 0;
}

int sceAppUtilSaveDataSlotSetParam_patched(unsigned int slotId, SceAppUtilSaveDataSlotParam* param, SceAppUtilSaveDataMountPoint* mountPoint) {
	LOG("Called sceAppUtilSaveDataSlotSetParam on slot %u", slotId);
	
	// Writing slot param file
	sprintf(fname, "ux0:/data/savegames/%s/SLOT%u/SlotParam.bin", titleid, slotId);
	SceUID fd;
	kuIoOpen(fname, SCE_O_WRONLY, &fd);
	if (fd < 0) return SCE_APPUTIL_ERROR_SAVEDATA_SLOT_NOT_FOUND;
	else{
		int i = 0;
		uint8_t* paramBuf = (uint8_t*)param;
		while (i < sizeof(SceAppUtilSaveDataSlotParam)){
			int len = (i + BUF_SIZE > sizeof(SceAppUtilSaveDataSlotParam)) ? sizeof(SceAppUtilSaveDataSlotParam) - i : BUF_SIZE;
			kuIoWrite(fd, &paramBuf[i], len);
			i += len;
		}
		kuIoClose(fd);
	}
	
	return 0;
}

int sceAppUtilSaveDataDataRemove_patched(SceAppUtilSaveDataFileSlot* slot, SceAppUtilSaveDataRemoveItem* files, unsigned int fileNum, SceAppUtilSaveDataMountPoint* mountPoint) {
	LOG("Called sceAppUtilSaveDataDataRemove on slot %u for %u files", slot->id, fileNum);
	
	// Removing savedata files
	int fileIdx = 0;
	while (fileIdx < fileNum){
		sprintf(fname, "ux0:/data/savegames/%s/SLOT%u/%s", titleid, slot->id, files[fileIdx].dataPath);
		switch (files[fileIdx].mode){
			case SCE_APPUTIL_SAVEDATA_DATA_REMOVE_MODE_DEFAULT:
				kuIoRemove(fname);
				break;
			default:
				kuIoRmdir(fname);
				break;
		}
		fileIdx++;
	}
	
	return 0;
}

int sceAppUtilSaveDataSlotCreate_patched(unsigned int slotId, SceAppUtilSaveDataSlotParam* param, SceAppUtilSaveDataMountPoint* mountPoint) {
	LOG("Called sceAppUtilSaveDataSlotCreate on slot %u", slotId);
	
	// Creating slot param file
	sprintf(fname, "ux0:/data/savegames/%s/SLOT%u/SlotParam.bin", titleid, slotId);
	SceUID fd;
	kuIoOpen(fname, SCE_O_WRONLY | SCE_O_CREAT, &fd);
	int i = 0;
	uint8_t* paramBuf = (uint8_t*)param;
	while (i < sizeof(SceAppUtilSaveDataSlotParam)){
		int len = (i + BUF_SIZE > sizeof(SceAppUtilSaveDataSlotParam)) ? sizeof(SceAppUtilSaveDataSlotParam) - i : BUF_SIZE;
		kuIoWrite(fd, &paramBuf[i], len);
		i += len;
	}
	kuIoClose(fd);
	
	return 0;
}

int sceAppUtilSaveDataSlotDelete_patched(unsigned int slotId, SceAppUtilSaveDataMountPoint* mountPoint) {
	LOG("Called sceAppUtilSaveDataSlotDelete on slot %u", slotId);
	
	// Deleting slot param file
	sprintf(fname, "ux0:/data/savegames/%s/SLOT%u/SlotParam.bin", titleid, slotId);
	kuIoRemove(fname);
	
	return 0;
}

void _start() __attribute__ ((weak, alias ("module_start")));
int module_start(SceSize argc, const void *args) {
	
	// Getting game Title ID
	sceAppMgrAppParamGetString(0, 12, titleid , 256);
	
	// Creating savegames directory if they do not exist
	kuIoMkdir("ux0:/data/savegames");
	sprintf(buffer, "ux0:/data/savegames/%s", titleid);
	kuIoMkdir(buffer);
	int i;
	for (i=0;i<=9;i++){
		sprintf(buffer, "ux0:/data/savegames/%s/SLOT%d", titleid, i);
		kuIoMkdir(buffer);
	}
	
	hookFunction(0x607647BA,sceAppUtilSaveDataDataSave_patched);
	hookFunction(0x93F0D89F,sceAppUtilSaveDataSlotGetParam_patched);
	hookFunction(0x98630136,sceAppUtilSaveDataSlotSetParam_patched);
	hookFunction(0xD1C6AB8E,sceAppUtilSaveDataDataRemove_patched);
	hookFunction(0x7E8FE96A,sceAppUtilSaveDataSlotCreate_patched);
	hookFunction(0x266A7646,sceAppUtilSaveDataSlotDelete_patched);
	
	return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args) {

	// Freeing hooks
	while (current_hook-- > 0){
		taiHookRelease(hooks[current_hook], refs[current_hook]);
	}


	return SCE_KERNEL_STOP_SUCCESS;
	
}