#define _PSP2_KERNEL_CLIB_H_ // Prevent incompatibility between libk and sceLibc
#include <vitasdk.h>
#include <taihen.h>
#include <libk/stdio.h>
#include <libk/stdarg.h>
#include <libk/string.h>

#define HOOKS_NUM   6     // Hooked functions num

static uint8_t current_hook = 0;
static SceUID hooks[HOOKS_NUM];
static tai_hook_ref_t refs[HOOKS_NUM];
static char fname[256];

void hookFunction(uint32_t nid, const void *func){
	hooks[current_hook] = taiHookFunctionImport(&refs[current_hook], TAI_MAIN_MODULE, TAI_ANY_LIBRARY, nid, func);
	current_hook++;
}

int sceAppUtilSaveDataDataSave_patched(SceAppUtilSaveDataFileSlot* slot, SceAppUtilSaveDataFile* files, unsigned int fileNum, SceAppUtilSaveDataMountPoint* mountPoint, SceSize* requiredSizeKB) {
	
	// Writing savedata files
	SceUID fd;
	uint32_t fileIdx = 0;
	while (fileIdx < fileNum){
		sprintf(fname, "savedata0:/%s", files[fileIdx].filePath);
		switch (files[fileIdx].mode){
			case SCE_APPUTIL_SAVEDATA_DATA_SAVE_MODE_FILE:
				fd = sceIoOpen(fname, SCE_O_WRONLY | SCE_O_CREAT, 0777);
				sceIoLseek(fd, files[fileIdx].offset, SEEK_SET);
				sceIoWrite(fd, (uint8_t*)files[fileIdx].buf, files[fileIdx].bufSize);
				sceIoClose(fd);
				break;
			default:
				sceIoMkdir(fname, 0777);
				break;
		}
		fileIdx++;
	}
	
	// Writing slot param file
	sprintf(fname, "savedata0:/SlotParam_%u.bin", slot->id);
	fd = sceIoOpen(fname, SCE_O_WRONLY | SCE_O_CREAT, 0777);
	sceIoWrite(fd, (uint8_t*)slot->slotParam, sizeof(SceAppUtilSaveDataSlotParam));
	sceIoClose(fd);
	
	return 0;
}

int sceAppUtilSaveDataSlotGetParam_patched(unsigned int slotId, SceAppUtilSaveDataSlotParam* param, SceAppUtilSaveDataMountPoint* mountPoint) {
	
	// Checking if slot param file exists
	sprintf(fname, "savedata0:/SlotParam_%u.bin", slotId);
	SceUID fd = sceIoOpen(fname, SCE_O_RDONLY, 0777);
	if (fd < 0) return SCE_APPUTIL_ERROR_SAVEDATA_SLOT_NOT_FOUND;
	else{
		sceIoRead(fd, (uint8_t*)param, sizeof(SceAppUtilSaveDataSlotParam));
		sceIoClose(fd);	
	}
	
	return 0;
}

int sceAppUtilSaveDataSlotSetParam_patched(unsigned int slotId, SceAppUtilSaveDataSlotParam* param, SceAppUtilSaveDataMountPoint* mountPoint) {
	
	// Writing slot param file
	sprintf(fname, "savedata0:/SlotParam_%u.bin", slotId);
	SceUID fd = sceIoOpen(fname, SCE_O_WRONLY, 0777);
	if (fd < 0) return SCE_APPUTIL_ERROR_SAVEDATA_SLOT_NOT_FOUND;
	else{
		sceIoWrite(fd, (uint8_t*)param, sizeof(SceAppUtilSaveDataSlotParam));
		sceIoClose(fd);
	}
	
	return 0;
}

int sceAppUtilSaveDataDataRemove_patched(SceAppUtilSaveDataFileSlot* slot, SceAppUtilSaveDataRemoveItem* files, unsigned int fileNum, SceAppUtilSaveDataMountPoint* mountPoint) {
	
	// Removing savedata files
	int fileIdx = 0;
	while (fileIdx < fileNum){
		sprintf(fname, "savedata0:/%s", files[fileIdx].dataPath);
		switch (files[fileIdx].mode){
			case SCE_APPUTIL_SAVEDATA_DATA_REMOVE_MODE_DEFAULT:
				sceIoRemove(fname);
				break;
			default:
				sceIoRmdir(fname);
				break;
		}
		fileIdx++;
	}
	
	return 0;
}

int sceAppUtilSaveDataSlotCreate_patched(unsigned int slotId, SceAppUtilSaveDataSlotParam* param, SceAppUtilSaveDataMountPoint* mountPoint) {
	
	// Creating slot param file
	sprintf(fname, "savedata0:/SlotParam_%u.bin", slotId);
	SceUID fd = sceIoOpen(fname, SCE_O_WRONLY | SCE_O_CREAT, 0777);
	sceIoWrite(fd, (uint8_t*)param, sizeof(SceAppUtilSaveDataSlotParam));
	sceIoClose(fd);
	
	return 0;
}

int sceAppUtilSaveDataSlotDelete_patched(unsigned int slotId, SceAppUtilSaveDataMountPoint* mountPoint) {
	
	// Deleting slot param file
	sprintf(fname, "savedata0:/SlotParam_%u.bin", slotId);
	sceIoRemove(fname);
	
	return 0;
}

void _start() __attribute__ ((weak, alias ("module_start")));
int module_start(SceSize argc, const void *args) {

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