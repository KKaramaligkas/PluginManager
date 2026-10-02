/* Exercise the production worker with failed PSP thread/semaphore syscalls. */
#include <string.h>
#include "psp/pspkernel.h"
#include "../src/app.h"
#include "../src/worker.h"
#include "../src/installer.h"
#include "../src/net.h"
#include "test.h"
int test_failures, test_checks;
app_t app;
static int failure, deletes, sema_deletes, status = 4;
SceUID sceKernelCreateSema(const char *n,int a,int b,int c,void *d) { return failure==1?-1:10; }
SceUID sceKernelCreateThread(const char *n,int (*f)(SceSize,void *),int p,int s,int a,void *v) { return failure==2?-1:11; }
int sceKernelStartThread(SceUID t,SceSize s,void *v) { return failure==3?-1:0; }
int sceKernelDeleteThread(SceUID t) { deletes++; return 0; }
int sceKernelDeleteSema(SceUID s) { sema_deletes++; return 0; }
int sceKernelWaitSema(SceUID s,int c,void *t) { return -1; }
int sceKernelSignalSema(SceUID s,int c) { return failure==4?-1:0; }
int sceKernelWaitThreadEnd(SceUID t,SceUInt *timeout) { CHECK(timeout==NULL); return 0; }
int sceKernelReferThreadStatus(SceUID t,SceKernelThreadInfo *i) { i->status=status; return failure==5?-1:0; }
int sceKernelDelayThread(unsigned int t) { return 0; }
unsigned int sceKernelGetSystemTimeLow(void) { return 0; }
void app_fill_install_ctx(install_ctx *ctx) { memset(ctx,0,sizeof(*ctx)); }
int image_png_valid(const char *p) { return 0; }
char *net_get(const char *u,int m,int *len,net_progress_fn cb,void *ud,char *err,int size) { return NULL; }
int net_download(const char *u,const char *p,net_progress_fn cb,void *ud,char *err,int size) { return -1; }

int main(void)
{
    for(failure=1;failure<=3;failure++) {
        deletes=sema_deletes=0;
        CHECK_INT(worker_start(),-1);
        CHECK_INT(worker_submit(JOB_INSTALL,"arkbrowser","ARK Browser"),-1);
        CHECK(!worker_busy());
        CHECK_INT(deletes,failure==3?1:0);
        CHECK_INT(sema_deletes,failure>=2?1:0);
    }
    failure=0; CHECK_INT(worker_start(),0);
    failure=4;
    CHECK_INT(worker_submit(JOB_INSTALL,"arkbrowser","ARK Browser"),-1);
    CHECK(!worker_busy());
    failure=0;
    CHECK_INT(worker_submit(JOB_INSTALL,"arkbrowser","ARK Browser"),0);
    CHECK_STR(job.stage,"Preparing installation");
    CHECK(worker_busy()); worker_cancel(); CHECK(job.cancel);
    status=PSP_THREAD_KILLED;
    CHECK_INT(worker_collect(),1);
    CHECK_INT(job.result,-1); CHECK(!job.cancel); CHECK(!worker_busy());
    CHECK(strstr(job.error,"worker stopped")!=NULL);
    CHECK_INT(worker_submit(JOB_INSTALL,"arkbrowser","ARK Browser"),-1);
    worker_stop(); status=4;
    CHECK_INT(worker_start(),0);
    CHECK_INT(worker_submit(JOB_INSTALL,"arkbrowser","ARK Browser"),0);
    failure=5; CHECK_INT(worker_collect(),1); CHECK_INT(job.result,-1);
    failure=0; worker_stop();
    printf("%d worker checks, %d failures\n",test_checks,test_failures);
    return test_failures?1:0;
}
