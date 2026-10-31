#ifndef VIDEO_STATUS_GALLERY_H
#define VIDEO_STATUS_GALLERY_H
#include <sys/prctl.h>
#include <sys/wait.h>
#include "camera_album_core.h"
#include "video_status_gallery_text.h"
struct panel_gallery {
    struct album_packet *display,*pending;
    pid_t pid;int fd,kind,cancelling;
    size_t used,wanted;
    unsigned revision;
    uint64_t deadline_ms,cancel_ms,selected_sequence;
};
static inline void panel_gallery_cancel(struct panel_gallery *g,uint64_t now) {
    if(g->pid&&!g->cancelling){kill(g->pid,SIGTERM);g->cancelling=1;g->cancel_ms=now;}
}
static inline int panel_gallery_spawn(struct panel_gallery *g,struct panel_view *v,uint64_t now) {
    if(!g->display)g->display=calloc(1,sizeof(*g->display));
    if(!g->pending)g->pending=calloc(1,sizeof(*g->pending));
    if(!g->display||!g->pending)return -1;
    char argument[128];int kind=(int)v->gallery_request;
    if(kind==2) {
        if(g->display->kind!=1||v->gallery_selected>=g->display->n)return -1;
        const struct album_meta *m=&g->display->entries[v->gallery_selected].meta;
        if(!album_id_valid(m->id))return -1;
        strcpy(argument,m->id);g->selected_sequence=m->sequence;
    } else snprintf(argument,sizeof(argument),"%u",v->gallery_page);
    int pipes[2];if(pipe2(pipes,O_CLOEXEC))return -1;
    if(fcntl(pipes[0],F_SETFL,O_NONBLOCK)){close(pipes[0]);close(pipes[1]);return -1;}
    pid_t parent=getpid(),pid=fork();
    if(!pid) {
        signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);signal(SIGHUP,SIG_DFL);
        if(prctl(PR_SET_PDEATHSIG,SIGTERM)||getppid()!=parent||dup2(pipes[1],STDOUT_FILENO)<0)_exit(127);
        close(pipes[0]);close(pipes[1]);
        execl("/usr/bin/camera_album","camera_album",kind==2?"--detail":"--page",argument,(char *)NULL);_exit(127);
    }
    close(pipes[1]);if(pid<0){close(pipes[0]);return -1;}
    memset(g->pending,0,sizeof(*g->pending));g->fd=pipes[0];g->pid=pid;g->kind=kind;
    g->revision=v->gallery_revision;g->used=0;g->wanted=40;g->deadline_ms=now+10000;g->cancelling=0;
    v->gallery_request=0;v->gallery_busy=1;v->gallery_items=0;return 0;
}
static inline int panel_gallery_packet_valid(const struct album_packet *p,int kind) {
    if(p->magic!=ALBUM_MAGIC||p->kind!=(unsigned)kind||p->status||p->n>ALBUM_PAGE||p->total>ALBUM_LIMIT)return 0;
    if(kind==1) {
        if(p->bytes!=ALBUM_PAGE_BYTES||p->page>=ALBUM_LIMIT||p->n+p->page*ALBUM_PAGE>p->total)return 0;
    } else if(p->bytes!=sizeof(*p)||p->n!=1||!memchr(p->json,0,sizeof(p->json)))return 0;
    for(unsigned i=0;i<p->n;i++)if(!memchr(p->entries[i].meta.id,0,128)||!album_id_valid(p->entries[i].meta.id))return 0;
    return 1;
}
static inline void panel_gallery_tick(struct panel_gallery *g,struct panel_view *v,uint64_t now) {
    if(g->pid&&(!v->gallery_open||v->gallery_request||g->revision!=v->gallery_revision||now>=g->deadline_ms))panel_gallery_cancel(g,now);
    if(g->pid) {
        unsigned budget=128*1024;
        while(budget&&g->used<g->wanted) {
            size_t bytes=g->wanted-g->used;if(bytes>budget)bytes=budget;
            ssize_t n=read(g->fd,(unsigned char *)g->pending+g->used,bytes);
            if(n<0&&(errno==EAGAIN||errno==EINTR))break;
            if(n<=0)break;
            g->used+=(size_t)n;budget-=(unsigned)n;
            if(g->used==40&&g->wanted==40) {
                unsigned expected=g->kind==1?ALBUM_PAGE_BYTES:sizeof(*g->pending);
                if(g->pending->magic!=ALBUM_MAGIC||g->pending->kind!=(unsigned)g->kind||
                   (g->pending->bytes!=expected&&!(g->pending->status&&g->pending->bytes==ALBUM_PAGE_BYTES))) {
                    panel_gallery_cancel(g,now);break;
                }
                g->wanted=g->pending->bytes;
            }
        }
        if(g->cancelling&&now-g->cancel_ms>1000)kill(g->pid,SIGKILL);
        int status=0;pid_t done=waitpid(g->pid,&status,WNOHANG);
        if(done==g->pid||(done<0&&errno!=EINTR)) {
            /* Exit can race the128KiB budget boundary; at most one pipe's
             * trailing bytes remain after the writer has exited. */
            while(g->used<g->wanted) {
                ssize_t n=read(g->fd,(unsigned char *)g->pending+g->used,g->wanted-g->used);
                if(n<=0)break;
                g->used+=(size_t)n;
            }
            int current=v->gallery_open&&g->revision==v->gallery_revision&&!v->gallery_request;
            int ok=done==g->pid&&WIFEXITED(status)&&!WEXITSTATUS(status)&&!g->cancelling&&
                g->used==g->wanted&&panel_gallery_packet_valid(g->pending,g->kind);
            if(current) {
                v->gallery_busy=0;v->gallery_error=!ok;
                if(ok) {
                    struct album_packet *old=g->display;g->display=g->pending;g->pending=old;v->gallery_data=g->display;
                    if(g->kind==1) {
                        v->gallery_page=g->display->page;v->gallery_pages=(g->display->total+ALBUM_PAGE-1)/ALBUM_PAGE;
                        v->gallery_items=g->display->n;
                    } else {
                        if(!g->display->entries[0].meta.sequence)g->display->entries[0].meta.sequence=g->selected_sequence;
                        char lines[256][49];unsigned count=panel_gallery_text_lines(g->display->json,lines,256);
                        v->gallery_json_pages=(count+19)/20;v->gallery_json_page=0;
                    }
                    v->gallery_revision++;
                }
                printf("GALLERY_RESULT kind=%d ok=%d page=%u items=%u bytes=%zu\n",g->kind,ok,v->gallery_page,v->gallery_items,g->used);fflush(stdout);
            }
            close(g->fd);g->fd=-1;g->pid=0;g->cancelling=0;
        }
    }
    if(v->gallery_open&&v->gallery_request&&!g->pid&&panel_gallery_spawn(g,v,now)) {
        v->gallery_request=0;v->gallery_busy=0;v->gallery_error=1;
    }
}
static inline void panel_gallery_stop(struct panel_gallery *g) {
    if(g->pid){kill(g->pid,SIGTERM);waitpid(g->pid,NULL,WNOHANG);}
    if(g->fd>=0)close(g->fd);
    free(g->display);free(g->pending);
    memset(g,0,sizeof(*g));g->fd=-1;
}
#endif
