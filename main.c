#define _PSP2_KERNEL_CLIB_H_ // Prevent incompatibility between libk and sceLibc
#include <vitasdkkern.h>
#include <taihen.h>
#include <libk/stdio.h>
#include <libk/stdarg.h>
#include <libk/string.h>
#include <psp2/apputil.h>
#include <psp2/appmgr.h>

#define HOOKS_NUM   12      // Hooked functions num
#define BUFFER_SIZE 2048   // Kernel buffer size
#define MOUNTPOINT  "ux0"  // Mountpoint to use

static uint8_t current_hook = 0;
static SceUID hooks[HOOKS_NUM];
static tai_hook_ref_t refs[HOOKS_NUM];
static char titleid[16];
static char fname[256], kfile[256];
static uint8_t workslot = 0;
static SceAppUtilSaveDataFile file;
static SceAppUtilSaveDataRemoveItem ktfile;
static SceAppUtilSaveDataSlotParam kslot;
static SceAppMgrSaveDataData kdata;
static SceAppMgrSaveDataSlot kslot2;
static SceAppMgrSaveDataDataDelete ktrash;
static SceAppMgrSaveDataSlotDelete ktslot;
static uint8_t kbuffer[BUFFER_SIZE];

#define OPEN_FILE   0
#define REMOVE_FILE 1
#define REMOVE_DIR  2
#define CREATE_DIR  3

static SceUID io_thd_id, io_request_mutex, io_result_mutex;

typedef struct{
	const char* file;
	int flags;
	SceUID fd;
	uint8_t type;
}ioRequest;

volatile ioRequest io_request;

static int io_thread(SceSize args, void *argp)
{
	
	for (;;){
		
		// Waiting for IO request
		ksceKernelWaitSema(io_request_mutex, 1, NULL);
		
		// Executing the received request
		switch(io_request.type){
			case OPEN_FILE:
				io_request.fd = ksceIoOpen(io_request.file, io_request.flags, 0777);
				break;
			case REMOVE_FILE:
				ksceIoRemove(io_request.file);
				break;
			case REMOVE_DIR:
				ksceIoRmdir(io_request.file);
				break;
			case CREATE_DIR:
				ksceIoMkdir(io_request.file, 0777);
				break;
			default:
				break;
		}
		
		// Sending results
		ksceKernelSignalSema(io_result_mutex, 1);
		
	}
	
	return 0;
	
}

static void execIoOp(uint8_t op){
	io_request.type = op;
	ksceKernelSignalSema(io_request_mutex, 1);
	ksceKernelWaitSema(io_result_mutex, 1, NULL);
}

static SceUID openFile(const char* file, int flags){
	io_request.file = file;
	io_request.flags = flags;
	execIoOp(OPEN_FILE);
	return io_request.fd;
}

static void removeFile(const char* file){
	io_request.file = file;
	execIoOp(REMOVE_FILE);
}

static void removeDir(const char* dir){
	io_request.file = dir;
	execIoOp(REMOVE_DIR);
}

static void createDir(const char* dir){
	io_request.file = dir;
	execIoOp(CREATE_DIR);
}

static char log_file[64];
void LOG(char* format, ...){
	char str[512] = { 0 };
	va_list va;

	va_start(va, format);
	vsnprintf(str, 512, format, va);
	va_end(va);
	
	SceUID fd;
	sprintf(log_file, "%s:/data/%s.log", MOUNTPOINT, titleid);
	fd = openFile(log_file, SCE_O_WRONLY | SCE_O_APPEND | SCE_O_CREAT);
	ksceIoWrite(fd, str, strlen(str));
	ksceIoWrite(fd, "\n", 1);
	ksceIoClose(fd);
}

void hookFunctionExport(uint32_t nid, const void *func, const char* module){
	hooks[current_hook] = taiHookFunctionExportForKernel(KERNEL_PID, &refs[current_hook], module, TAI_ANY_LIBRARY, nid, func);
	LOG("hook #%d returned 0x%X", current_hook, hooks[current_hook]);
	current_hook++;
}

void bufferedWrite(SceUID fd, SceOff offs, uint8_t* buf, int size){
	ksceIoLseek(fd, offs, SEEK_SET);
	int i = 0;
	while (i < size){
		int bufsize = (i + BUFFER_SIZE > size) ? (size - i) : BUFFER_SIZE;
		ksceKernelMemcpyUserToKernel(kbuffer, (uintptr_t)(buf + i), bufsize);
		ksceIoWrite(fd, kbuffer, bufsize);
		i += bufsize;
	}
}

SceUID ksceIoOpen_patched(const char* file, int flags, SceMode mode) {
	
	SceUID ret = 0;

	// Attempting to access savedata, redirecting it
	if (strncmp(file, "savedata0:", 10) == 0){
		sprintf(fname, "%s:/data/savegames/%s/SLOT%d/%s", MOUNTPOINT, titleid, workslot, (file[10] == '/') ? &file[11] : &file[10]);
		ret = openFile(fname, flags);
		LOG("Redirecting %s to %s via ksceIoOpen (flags: 0x%X, fd: 0x%X)", file, fname, flags, ret);
	}else ret = TAI_CONTINUE(SceUID, refs[0], file, flags, mode);
	
	return ret;
	
}

SceUID ksceIoOpen2_patched(SceUID pid, const char* file, int flags, SceMode mode) {
	
	SceUID ret = 0;
	
	// Attempting to access savedata, redirecting it
	if (strncmp(file, "savedata0:", 10) == 0){
		sprintf(fname, "%s:/data/savegames/%s/SLOT%d/%s", MOUNTPOINT, titleid, workslot, (file[10] == '/') ? &file[11] : &file[10]);
		ret = openFile(fname, flags);
		LOG("Redirecting %s to %s via ksceIoOpen2 (flags: 0x%X, fd: 0x%X)", file, fname, flags, ret);
	}else ret = TAI_CONTINUE(SceUID, refs[1], pid, file, flags, mode);
	
	return ret;
	
}

SceUID ksceKernelLaunchApp_patched(char *tid, uint32_t flags, char *path, void *unk) {
	
	// Not interested in SceShell and other Sony apps
	if ((strlen(tid) == 9) && (strncmp(tid, "NPXS", 4) != 0)){
		sprintf(titleid, tid);
	
		// Creating savegames directories if they do not exist
		sprintf(fname, "%s:/data/savegames", MOUNTPOINT);
		createDir(fname);
		sprintf(fname, "%s:/data/savegames/%s", MOUNTPOINT, tid);
		createDir(fname);
		int i;
		for (i=0;i<=9;i++){
			sprintf(fname, "%s:/data/savegames/%s/SLOT%d", MOUNTPOINT, tid, i);
			createDir(fname);
		}
	}
	
	return TAI_CONTINUE(SceUID, refs[2], tid, flags, path, unk);
}

int ksceIoRemove_patched(const char *file) {
	
	SceUID ret = 0;
	
	// Attempting to access savedata, redirecting it
	if (strncmp(file, "savedata0:", 10) == 0){
		sprintf(fname, "%s:/data/savegames/%s/SLOT%d/%s", MOUNTPOINT, titleid, workslot, (file[10] == '/') ? &file[11] : &file[10]);
		removeFile(fname);
		LOG("Redirecting %s to %s via ksceIoRemove", file, fname);
	}else ret = TAI_CONTINUE(int, refs[3], file);
	
	return ret;
}

int ksceIoRmdir_patched(const char *file) {
	
	SceUID ret = 0;
	
	// Attempting to access savedata, redirecting it
	if (strncmp(file, "savedata0:", 10) == 0){
		sprintf(fname, "%s:/data/savegames/%s/SLOT%d/%s", MOUNTPOINT, titleid, workslot, (file[10] == '/') ? &file[11] : &file[10]);
		removeDir(fname);
		LOG("Redirecting %s to %s via ksceIoRmdir", file, fname);
	}else ret = TAI_CONTINUE(int, refs[4], file);
	
	return ret;
}

int ksceIoMkdir_patched(const char *file, SceMode mode) {
	
	SceUID ret = 0;
	
	// Attempting to access savedata, redirecting it
	if (strncmp(file, "savedata0:", 10) == 0){
		sprintf(fname, "%s:/data/savegames/%s/SLOT%d/%s", MOUNTPOINT, titleid, workslot, (file[10] == '/') ? &file[11] : &file[10]);
		createDir(fname);
		LOG("Redirecting %s to %s via ksceIoMkdir", file, fname);
	}else ret = TAI_CONTINUE(int, refs[5], file, mode);
	
	return ret;
}

int _sceAppMgrSaveDataDataSave_patched(SceAppMgrSaveDataData* data) {
	
	// Writing savedata files
	uint32_t state;
	ENTER_SYSCALL(state);
	ksceKernelMemcpyUserToKernel(&kdata, (uintptr_t)data, sizeof(SceAppMgrSaveDataData));
	SceUID fd;
	uint32_t fileIdx = 0;
	while (fileIdx < kdata.fileNum){	
		ksceKernelMemcpyUserToKernel(&file, (uintptr_t)(kdata.files + fileIdx * sizeof(SceAppUtilSaveDataFile)), sizeof(SceAppUtilSaveDataFile));
		ksceKernelStrncpyUserToKernel(kfile, (uintptr_t)(file.filePath), 64);
		sprintf(fname, "%s:/data/savegames/%s/SLOT%d/%s", MOUNTPOINT, titleid, workslot, kfile);
		switch (file.mode){
			case SCE_APPUTIL_SAVEDATA_DATA_SAVE_MODE_FILE:
				fd = openFile(fname, SCE_O_WRONLY | SCE_O_CREAT);
				bufferedWrite(fd, file.offset, file.buf, file.bufSize);
				ksceIoClose(fd);
				break;
			default:
				createDir(fname);
				break;
		}
		fileIdx++;
	}
	
	// Writing slot param file
	sprintf(fname, "%s:/data/savegames/%s/SLOT%d/SlotParam_%u.bin", MOUNTPOINT, titleid, workslot, kdata.slotId);
	fd = openFile(fname, SCE_O_WRONLY | SCE_O_CREAT);
	ksceKernelMemcpyUserToKernel(&kslot, (uintptr_t)(kdata.slotParam), sizeof(SceAppUtilSaveDataSlotParam));
	ksceIoWrite(fd, (uint8_t*)&kslot, sizeof(SceAppUtilSaveDataSlotParam));
	ksceIoClose(fd);
	EXIT_SYSCALL(state);
	
	return 0;
	
}

int _sceAppMgrSaveDataSlotGetParam_patched(SceAppMgrSaveDataSlot* data) {
	
	uint32_t state;
	ENTER_SYSCALL(state);
	ksceKernelMemcpyUserToKernel(&kslot2, (uintptr_t)data, sizeof(SceAppMgrSaveDataSlot));
	sprintf(fname, "%s:/data/savegames/%s/SLOT%d/SlotParam_%u.bin", MOUNTPOINT, titleid, workslot, kslot2.slotId);
	SceUID fd = fd = openFile(fname, SCE_O_RDONLY);
	if (fd < 0) return SCE_APPUTIL_ERROR_SAVEDATA_SLOT_NOT_FOUND;
	else{
		ksceIoRead(fd, (uint8_t*)&kslot, sizeof(SceAppUtilSaveDataSlotParam));
		ksceIoClose(fd);	
	}
	ksceKernelMemcpyKernelToUser((uintptr_t)&data->slotParam, &kslot, sizeof(SceAppUtilSaveDataSlotParam));
	EXIT_SYSCALL(state);
	
	return 0;
	
}

int _sceAppMgrSaveDataSlotSetParam_patched(SceAppMgrSaveDataSlot* data) {
	
	uint32_t state;
	ENTER_SYSCALL(state);
	ksceKernelMemcpyUserToKernel(&kslot2, (uintptr_t)data, sizeof(SceAppMgrSaveDataSlot));
	sprintf(fname, "%s:/data/savegames/%s/SLOT%d/SlotParam_%u.bin", MOUNTPOINT, titleid, workslot, kslot2.slotId);
	SceUID fd = openFile(fname, SCE_O_WRONLY);
	if (fd < 0) return SCE_APPUTIL_ERROR_SAVEDATA_SLOT_NOT_FOUND;
	else{
		ksceIoWrite(fd, (uint8_t*)&kslot2.slotParam, sizeof(SceAppUtilSaveDataSlotParam));
		ksceIoClose(fd);	
	}
	EXIT_SYSCALL(state);
	
	return 0;
}

int _sceAppMgrSaveDataDataRemove_patched(SceAppMgrSaveDataDataDelete* data) {
	
	uint32_t state;
	ENTER_SYSCALL(state);
	ksceKernelMemcpyUserToKernel(&ktrash, (uintptr_t)data, sizeof(SceAppMgrSaveDataDataDelete));
	
	// Removing savedata files
	int fileIdx = 0;
	while (fileIdx < ktrash.fileNum){
		ksceKernelMemcpyUserToKernel(&ktfile, (uintptr_t)(ktrash.files + fileIdx * sizeof(SceAppUtilSaveDataRemoveItem)), sizeof(SceAppUtilSaveDataRemoveItem));
		ksceKernelStrncpyUserToKernel(kfile, (uintptr_t)(ktfile.dataPath), 64);
		sprintf(fname, "%s:/data/savegames/%s/SLOT%d/%s", MOUNTPOINT, titleid, workslot, kfile);
		switch (ktfile.mode){
			case SCE_APPUTIL_SAVEDATA_DATA_REMOVE_MODE_DEFAULT:
				removeFile(fname);
				break;
			default:
				removeDir(fname);
				break;
		}
		fileIdx++;
	}
	
	// Deleting slot param file
	sprintf(fname, "%s:/data/savegames/%s/SLOT%d/SlotParam_%u.bin", MOUNTPOINT, titleid, workslot, ktrash.slotId);
	removeFile(fname);
	
	EXIT_SYSCALL(state);
	
	return 0;
}

int _sceAppMgrSaveDataSlotDelete_patched(SceAppMgrSaveDataSlotDelete* data) {
	
	uint32_t state;
	ENTER_SYSCALL(state);
	ksceKernelMemcpyUserToKernel(&ktslot, (uintptr_t)data, sizeof(SceAppMgrSaveDataSlotDelete));
	
	// Removing slot param file
	sprintf(fname, "%s:/data/savegames/%s/SLOT%d/SlotParam_%u.bin", MOUNTPOINT, titleid, workslot, ktslot.slotId);
	removeFile(fname);
	EXIT_SYSCALL(state);
	
	return 0;
}

int _sceAppMgrSaveDataSlotCreate_patched(SceAppMgrSaveDataSlot* data) {
	
	uint32_t state;
	ENTER_SYSCALL(state);
	ksceKernelMemcpyUserToKernel(&kslot2, (uintptr_t)data, sizeof(SceAppMgrSaveDataSlot));
	sprintf(fname, "%s:/data/savegames/%s/SLOT%d/SlotParam_%u.bin", MOUNTPOINT, titleid, workslot, kslot2.slotId);
	SceUID fd = openFile(fname, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC);
	if (fd < 0) return SCE_APPUTIL_ERROR_SAVEDATA_SLOT_NOT_FOUND;
	else{
		ksceIoWrite(fd, (uint8_t*)&kslot2.slotParam, sizeof(SceAppUtilSaveDataSlotParam));
		ksceIoClose(fd);	
	}
	EXIT_SYSCALL(state);
	
	return 0;
}

void _start() __attribute__ ((weak, alias ("module_start")));
int module_start(SceSize argc, const void *args) {
	
	sprintf(titleid, "UNKNOWN");
	
	// Starting I/O mutexes
	io_request_mutex = ksceKernelCreateSema("io_request", 0, 0, 1, NULL);
	io_result_mutex = ksceKernelCreateSema("io_result", 0, 0, 1, NULL);
	
	// Starting a secondary thread to hold kernel privileges
	io_thd_id = ksceKernelCreateThread("io_thread", io_thread, 0x3C, 0x1000, 0, 0x10000, 0);
	ksceKernelStartThread(io_thd_id, 0, NULL);

	hookFunctionExport(0x75192972,ksceIoOpen_patched,"SceIofilemgr");
	hookFunctionExport(0xC3D34965,ksceIoOpen2_patched,"SceIofilemgr");
	hookFunctionExport(0x71CF71FD,ksceKernelLaunchApp_patched,"SceProcessmgr");
	hookFunctionExport(0x0D7BB3E1,ksceIoRemove_patched,"SceIofilemgr");
	hookFunctionExport(0x1CC9C634,ksceIoRmdir_patched,"SceIofilemgr");
	hookFunctionExport(0x7F710B25,ksceIoMkdir_patched,"SceIofilemgr");
	hookFunctionExport(0xB81777B7,_sceAppMgrSaveDataDataSave_patched,"SceAppMgr");
	hookFunctionExport(0x74D789E2,_sceAppMgrSaveDataSlotGetParam_patched,"SceAppMgr");
	hookFunctionExport(0x0E216486,_sceAppMgrSaveDataSlotSetParam_patched,"SceAppMgr");
	hookFunctionExport(0xA579A39E,_sceAppMgrSaveDataDataRemove_patched,"SceAppMgr");
	hookFunctionExport(0x191CF6B1,_sceAppMgrSaveDataSlotDelete_patched,"SceAppMgr");
	hookFunctionExport(0xC48833AA,_sceAppMgrSaveDataSlotCreate_patched,"SceAppMgr");
	
	return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args) {

	// Freeing hooks
	while (current_hook-- > 0){
		taiHookReleaseForKernel(hooks[current_hook], refs[current_hook]);
	}


	return SCE_KERNEL_STOP_SUCCESS;
	
}