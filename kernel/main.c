#define _PSP2_KERNEL_CLIB_H_ // Prevent incompatibility between libk and sceLibc
#include <vitasdkkern.h>
#include <taihen.h>
#include <libk/stdio.h>
#include <libk/stdarg.h>
#include <libk/string.h>

#define HOOKS_NUM   5      // Hooked functions num

static uint8_t current_hook = 0;
static SceUID hooks[HOOKS_NUM];
static tai_hook_ref_t refs[HOOKS_NUM];
static char titleid[16];
static char fname[256];
static uint8_t workslot = 0;

#define OPEN_FILE   0
#define REMOVE_FILE 1
#define REMOVE_DIR  2

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

static char log_file[64];
void LOG(char* format, ...){
	char str[512] = { 0 };
	va_list va;

	va_start(va, format);
	vsnprintf(str, 512, format, va);
	va_end(va);
	
	SceUID fd;
	sprintf(log_file, "ux0:/data/%s.log", titleid);
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

SceUID ksceIoOpen_patched(const char* file, int flags, SceMode mode) {
	
	SceUID ret = 0;

	// Attempting to access savedata, redirecting it
	if (strncmp(file, "savedata0:", 10) == 0){
		sprintf(fname, "ux0:/data/savegames/%s/SLOT%d/%s", titleid, workslot, (file[10] == '/') ? &file[11] : &file[10]);
		ret = openFile(fname, flags);
		LOG("Redirecting %s to %s via ksceIoOpen (flags: 0x%X, fd: %d)", file, fname, flags, ret);
	}else ret = TAI_CONTINUE(SceUID, refs[0], file, flags, mode);
	
	return ret;
	
}

SceUID ksceIoOpen2_patched(SceUID pid, const char* file, int flags, SceMode mode) {
	
	SceUID ret = 0;
	
	// Attempting to access savedata, redirecting it
	if (strncmp(file, "savedata0:", 10) == 0){
		sprintf(fname, "ux0:/data/savegames/%s/SLOT%d/%s", titleid, workslot, (file[10] == '/') ? &file[11] : &file[10]);
		ret = openFile(fname, flags);
		LOG("Redirecting %s to %s via ksceIoOpen2 (flags: 0x%X, fd: %d)", file, fname, flags, ret);
	}else ret = TAI_CONTINUE(SceUID, refs[1], pid, file, flags, mode);
	
	return ret;
	
}

SceUID ksceIoOpenAsync_patched(const char* file, int flags, SceMode mode) {
	
	SceUID ret = TAI_CONTINUE(SceUID, refs[2], file, flags, mode);

	// Attempting to access savedata, redirecting it
	if (strncmp(file, "savedata0:", 10) == 0){
		sprintf(fname, "ux0:/data/savegames/%s/SLOT%d/%s", titleid, workslot, (file[10] == '/') ? &file[11] : &file[10]);
		LOG("Redirecting %s to %s via ksceIoOpenAsync (flags: 0x%X, fd: %d)", file, fname, flags, ret);
	}
	
	return ret;
	
}

typedef struct ksceIoOpen3_args{
	const char *file;
	int flags;
	SceMode mode;
}ksceIoOpen3_args;

SceUID ksceIoOpen3_patched(ksceIoOpen3_args* args) {
	
	SceUID ret = 0;
	const char* file = args->file;
	int flags = args->flags;
	
	// Attempting to access savedata, redirecting it
	if (strncmp(file, "savedata0:", 10) == 0){
		sprintf(fname, "ux0:/data/savegames/%s/SLOT%d/%s", titleid, workslot, (file[10] == '/') ? &file[11] : &file[10]);
		ret = openFile(fname, flags);
		LOG("Redirecting %s to %s via ksceIoOpen3 (flags: 0x%X, fd: %d)", file, fname, flags, ret);
	}else ret = TAI_CONTINUE(SceUID, refs[3], args);
	
	return ret;
	
}

SceUID ksceKernelLaunchApp_patched(char *tid, uint32_t flags, char *path, void *unk) {
	
	// Not interested in SceShell and other Sony apps
	if ((strlen(tid) == 9) && (strncmp(tid, "NPXS", 4) != 0)){
		sprintf(titleid, tid);
	
		// Creating savegames directories if they do not exist
		ksceIoMkdir("ux0:/data/savegames", 0777);
		sprintf(fname, "ux0:/data/savegames/%s", tid);
		ksceIoMkdir(fname, 0777);
		int i;
		for (i=0;i<=9;i++){
			sprintf(fname, "ux0:/data/savegames/%s/SLOT%d", tid, i);
			ksceIoMkdir(fname, 0777);
			sprintf(fname, "ux0:/data/savegames/%s/SLOT%d/sce_sys", tid, i);
			ksceIoMkdir(fname, 0777);
		}
	}
	
	return TAI_CONTINUE(SceUID, refs[4], tid, flags, path, unk);
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
	hookFunctionExport(0x451001DE,ksceIoOpenAsync_patched,"SceIofilemgr");
	hookFunctionExport(0x0E518FA9,ksceIoOpen3_patched,"SceIofilemgr");
	hookFunctionExport(0x71CF71FD,ksceKernelLaunchApp_patched,"SceProcessmgr");
	
	return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize argc, const void *args) {

	// Freeing hooks
	while (current_hook-- > 0){
		taiHookReleaseForKernel(hooks[current_hook], refs[current_hook]);
	}


	return SCE_KERNEL_STOP_SUCCESS;
	
}