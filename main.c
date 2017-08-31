#define _PSP2_KERNEL_CLIB_H_ // Prevent incompatibility between libk and sceLibc
#include <vitasdk.h>
#include <taihen.h>
#include <libk/stdio.h>
#include <libk/stdarg.h>
#include <libk/string.h>
#include <kuio.h>

#define HOOKS_NUM   12     // Hooked functions num
#define BUF_SIZE    2048   // Savedata buffer size
#define MAX_HANDLES 16     // Maximum number of kuIo handles opened

static uint8_t current_hook = 0;
static SceUID hooks[HOOKS_NUM];
static tai_hook_ref_t refs[HOOKS_NUM];
static char buffer[BUF_SIZE];
static char titleid[16];
static char fname[256];
static uint8_t workslot = 0;
static SceUID kuio_handles[MAX_HANDLES];

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

void kuIoLongWrite(SceUID fd, uint8_t* buf, SceSize size){
	int i = 0;
	uint8_t* data = (uint8_t*)buf;
	while (i < size){
		int len = (i + BUF_SIZE > size) ? size - i : BUF_SIZE;
		kuIoWrite(fd, &data[i], len);
		i += len;
	}
}

void kuIoLongRead(SceUID fd, uint8_t* buf, SceSize size){
	int i = 0;
	uint8_t* data = (uint8_t*)buf;
	while (i < size){
		int len = (i + BUF_SIZE > size) ? size - i : BUF_SIZE;
		kuIoRead(fd, &data[i], len);
		i += len;
	}
}

void lockHandle(SceUID handle){
	int i = 0;
	while (kuio_handles[i] != 0){
		i++;
	}
	kuio_handles[i] = handle;
}

void unlockHandle(SceUID handle){
	int i = 0;
	while (kuio_handles[i] != handle){
		i++;
	}
	kuio_handles[i] = 0;
}

uint8_t checkHandle(SceUID handle){
	int i = 0;
	while (i < MAX_HANDLES){
		if (handle == kuio_handles[i]) return 1;
		i++;
	}
	return 0;
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
				kuIoLongWrite(fd, (uint8_t*)files[fileIdx].buf, files[fileIdx].bufSize);
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
	kuIoLongWrite(fd, (uint8_t*)slot->slotParam, sizeof(SceAppUtilSaveDataSlotParam));
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
		kuIoLongRead(fd, (uint8_t*)param, sizeof(SceAppUtilSaveDataSlotParam));
		kuIoClose(fd);	
	}
	
	// Setting working slot
	workslot = slotId;
	
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
		kuIoLongWrite(fd, (uint8_t*)param, sizeof(SceAppUtilSaveDataSlotParam));
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
	kuIoLongWrite(fd, (uint8_t*)param, sizeof(SceAppUtilSaveDataSlotParam));
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

SceUID sceIoOpen_patched(const char *file, int flags, SceMode mode) {
	
	SceUID ret = 0;
	
	// Attempting to access savedata, redirecting it
	if (strncmp(file, "savedata0:", 10) == 0){
		LOG("Redirecting %s via sceIoOpen", file);
		sprintf(fname, "ux0:/data/savegames/%s/SLOT%d/%s", titleid, workslot, &file[10]);
		kuIoOpen(fname, flags, &ret);
		lockHandle(ret);
	}else{
		ret = TAI_CONTINUE(SceUID, refs[6], file, flags, mode);
	}
	
	return ret;
	
}

int sceIoClose_patched(SceUID fd) {	
	if (checkHandle(fd)){
		unlockHandle(fd);
		kuIoClose(fd);
	}else TAI_CONTINUE(int, refs[7], fd);
	return 0;	
}

int sceIoWrite_patched(SceUID fd, const void *data, SceSize size) {	
	if (checkHandle(fd)) kuIoLongWrite(fd, (uint8_t*)data, size);
	else TAI_CONTINUE(int, refs[8], fd, data, size);
	return size;
}

int sceIoRead_patched(SceUID fd, const void *data, SceSize size) {	
	if (checkHandle(fd)) kuIoLongRead(fd, (uint8_t*)data, size);
	else TAI_CONTINUE(int, refs[9], fd, data, size);
	return size;
}

SceOff sceIoLseek_patched(SceUID fd, SceOff offset, int whence) {
	SceOff ret;
	if (checkHandle(fd)){
		kuIoLseek(fd, offset, whence);
		kuIoTell(fd, &ret);
	}else ret =  TAI_CONTINUE(SceOff, refs[10], fd, offset, whence);
	return ret;	
}

int sceIoLseek32_patched(SceUID fd, SceOff offset, int whence) {	
	SceOff ret;
	if (checkHandle(fd)){
		kuIoLseek(fd, offset, whence);
		kuIoTell(fd, &ret);
	}else ret =  TAI_CONTINUE(SceOff, refs[11], fd, offset, whence);
	return ret;
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
	hookFunction(0x6C60AC61,sceIoOpen_patched);
	hookFunction(0xC70B8886,sceIoClose_patched);
	hookFunction(0x34EFD876,sceIoWrite_patched);
	hookFunction(0xFDB32293,sceIoRead_patched);
	hookFunction(0x99BA173E,sceIoLseek_patched);
	hookFunction(0x49252B9B,sceIoLseek32_patched);
	
	return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args) {

	// Freeing hooks
	while (current_hook-- > 0){
		taiHookRelease(hooks[current_hook], refs[current_hook]);
	}


	return SCE_KERNEL_STOP_SUCCESS;
	
}