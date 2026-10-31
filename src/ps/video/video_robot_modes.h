#ifndef VIDEO_ROBOT_MODES_H
#define VIDEO_ROBOT_MODES_H
/* Included by the sole UART owner after current_status/save_job definitions. */
enum mode_transaction {MT_NONE,MT_CONFIG_TRACK,MT_CONFIG_CYCLE,MT_TRACK_START,
    MT_TRACK_REFRESH,MT_TRACK_HOLD,MT_CYCLE_START,MT_CYCLE_STATUS,MT_CYCLE_STOP};
static struct {
    struct robot_mode_config config;
    struct robot_track_filter filter;
    unsigned transaction,expect,id,value,inspect,track_armed,track_region,stop_requested,error;
    unsigned cycle_active,cycle_stage,cycle_reason,cycle_limit,cycle_completed,cycle_stopping;
    uint64_t due,next_sample,last_track_wire,lease_until,last_reply,settle_until;
    unsigned long long control_nonce;
    pid_t owner;
    /* Operator intent is RAM-only. Never restore automatic motion at boot. */
    unsigned auto_mode,auto_waiting,auto_latched,auto_candidate,auto_has_job;
    uint64_t auto_until,auto_sample,auto_since,auto_quiet,auto_cooldown;
    unsigned long long auto_nonce;
    pid_t auto_owner;
} modes;
static void mode_auto_clear(void){
    modes.auto_mode=modes.auto_waiting=modes.auto_candidate=0;
    modes.auto_until=modes.auto_nonce=0;modes.auto_owner=0;
    modes.auto_since=modes.auto_quiet=0;
}
static int mode_transaction_busy(void){return modes.transaction||modes.inspect||ms()<modes.settle_until;}
static unsigned mode_allocate_id(void){wire_sequence=(wire_sequence+1u)&65535u;return wire_sequence;}
static int mode_active(void){return job.mode!=ROBOT_MODE_GRAB&&robot_inflight(&job);}
static void mode_state_json(char *out,size_t cap){
    uint64_t now=ms();
    snprintf(out,cap,"\"mode\":%u,\"transaction_pending\":%u,\"track_armed\":%u,\"track_region\":%u,"
        "\"track_known\":%u,\"track_mask\":%u,\"cycle_known\":%u,\"cycle_ready\":%u,"
        "\"cycle_active\":%u,\"cycle_stage\":%u,\"cycle_reason\":%u,\"cycle_limit\":%u,"
        "\"cycle_completed\":%u,\"cycle_stopping\":%u,\"mode_error\":%u,\"lease_valid\":%u,\"reply_age_ms\":%llu,"
        "\"auto_mode\":%u,\"auto_waiting\":%u,\"auto_latched\":%u",
        job.mode,mode_transaction_busy()?1u:0u,modes.track_armed,modes.track_region,
        modes.config.track_known,modes.config.track_mask,modes.config.cycle_known,modes.config.cycle_ready,
        modes.cycle_active,modes.cycle_stage,modes.cycle_reason,modes.cycle_limit,modes.cycle_completed,
        modes.cycle_stopping,modes.error,mode_active()&&now<modes.lease_until?1u:0u,
        (unsigned long long)(modes.last_reply?now-modes.last_reply:UINT32_MAX),
        modes.auto_mode,modes.auto_waiting,modes.auto_latched);
}
static void mode_end(unsigned phase,unsigned error,const char *event){
    if(phase==ROBOT_UNKNOWN||phase==ROBOT_FAILED)mode_auto_clear();
    job.phase=phase;modes.error=error;modes.track_armed=0;modes.lease_until=0;modes.owner=0;
    modes.stop_requested=0;
    if(save_job())persist_fault=1;
    log_event(event);
}
static void mode_unknown(unsigned error,const char *event){
    modes.transaction=0;modes.inspect=0;modes.track_region=0;
    mode_end(ROBOT_UNKNOWN,error,event);
}
static int mode_issue(unsigned kind,unsigned type,unsigned value,unsigned expect,unsigned id,uint64_t now){
    if(modes.transaction||serial_fd<0)return -1;
    unsigned char frame[8],payload=(unsigned char)value;
    if(robot_encode_request(frame,sizeof(frame),type,id,&payload,1)!=8)return -1;
    size_t done=0;uint64_t end=ms()+300;
    while(done<8&&ms()<end&&!stopping){ssize_t n=write(serial_fd,frame+done,8-done);
        if(n>0)done+=(size_t)n;
        else if(n<0&&errno!=EAGAIN&&errno!=EINTR)break;
        else {struct pollfd p={serial_fd,POLLOUT,0};poll(&p,1,10);}}
    if(done!=8){
        if(mode_active())mode_unknown(RM_IO,"mode_partial_send_unknown");
        else {modes.inspect=0;modes.error=RM_IO;}
        return -1;
    }
    tx_frames++;modes.transaction=kind;modes.expect=expect;modes.id=id;modes.value=value;
    modes.due=ms()+((kind==MT_CONFIG_TRACK||kind==MT_CONFIG_CYCLE)?650:450);
    if(kind==MT_CONFIG_TRACK||kind==MT_CONFIG_CYCLE){
        printf("ROBOT_CONFIG_TX type=%02x id=%u expect=%02x deadline_ms=650\n",type,id,expect);fflush(stdout);}
    if(type==0x30)modes.last_track_wire=now;
    return 0;
}
static int mode_start(unsigned mode,unsigned value,unsigned long long nonce,pid_t owner){
    if(!nonce||persist_fault||regions_fault||serial_fd<0||robot_blocked(&job)||mode_transaction_busy())return -1;
    if(mode==ROBOT_MODE_TRACK){
        struct panel_status s;if(!current_status(&s))return -1;
        unsigned target=robot_track_region(&s);
        if(value!=target||!value||value>12||!modes.config.track_known||!(modes.config.track_mask&(1u<<(value-1))))return -1;
        memset(&modes.filter,0,sizeof(modes.filter));(void)robot_track_observe(&modes.filter,&s,ms());
    } else if(mode!=ROBOT_MODE_CYCLE||value>255||!modes.config.cycle_known||!modes.config.cycle_ready)return -1;
    modes.error=0;modes.control_nonce=nonce;modes.owner=owner;modes.lease_until=ms()+900;
    modes.cycle_active=modes.cycle_stage=modes.cycle_reason=modes.cycle_completed=modes.cycle_stopping=0;
    modes.track_region=0;modes.track_armed=mode==ROBOT_MODE_TRACK;
    modes.stop_requested=0;
    unsigned cached=job.last_grab_id,known=job.last_grab_known;
    job=(struct robot_job){.id=mode_allocate_id(),.phase=ROBOT_WAITING,.nonce=nonce,.protocol=3,.mode=mode,.value=value,
        .last_grab_id=cached,.last_grab_known=known};
    if(save_job()){persist_fault=1;job.phase=ROBOT_UNKNOWN;modes.track_armed=0;return -1;}
    uint64_t now=ms();
    /* A slow fsync before the first byte may consume the initial UI lease. */
    modes.lease_until=now+900;
    if(mode_issue(mode==ROBOT_MODE_TRACK?MT_TRACK_START:MT_CYCLE_START,
       mode==ROBOT_MODE_TRACK?0x30:0x40,value,mode==ROBOT_MODE_TRACK?0x25:0x27,job.id,now))return -1;
    modes.cycle_limit=mode==ROBOT_MODE_CYCLE?value:0;
    log_event(mode==ROBOT_MODE_TRACK?"track_user_started":"cycle_user_started");return 0;
}
static int mode_client_command(const char *text,pid_t peer){
    unsigned value;unsigned long long nonce;char extra;
    if(sscanf(text,"lease %16llx %c",&nonce,&extra)==1){
        if(modes.auto_mode&&nonce&&nonce==modes.auto_nonce&&peer==modes.auto_owner){
            modes.auto_until=ms()+900;
            if(mode_active())modes.lease_until=ms()+900;
            return 0;
        }
        if(!nonce||nonce!=modes.control_nonce||peer!=modes.owner||!mode_active()||job.phase==ROBOT_UNKNOWN)return -1;
        modes.lease_until=ms()+900;return 0;
    }
    if(sscanf(text,"inspect %16llx %c",&nonce,&extra)==1){
        if(!nonce||modes.auto_mode||robot_blocked(&job)||mode_transaction_busy()||persist_fault||serial_fd<0)return -1;
        modes.config=(struct robot_mode_config){0};modes.inspect=1;modes.control_nonce=nonce;modes.error=0;return 0;
    }
    if(sscanf(text,"track-start %u %16llx %c",&value,&nonce,&extra)==2){
        if(modes.auto_mode)return -1;
        if(nonce&&nonce==job.nonce)return job.mode==ROBOT_MODE_TRACK&&value==job.value?0:-1;
        return mode_start(ROBOT_MODE_TRACK,value,nonce,peer);
    }
    if(sscanf(text,"cycle-start %u %16llx %c",&value,&nonce,&extra)==2){
        if(modes.auto_mode)return -1;
        if(nonce&&nonce==job.nonce)return job.mode==ROBOT_MODE_CYCLE&&value==job.value?0:-1;
        return mode_start(ROBOT_MODE_CYCLE,value,nonce,peer);
    }
    unsigned automatic=0;
    if(sscanf(text,"auto-track %16llx %c",&nonce,&extra)==1)automatic=1;
    else if(sscanf(text,"auto-grab %16llx %c",&nonce,&extra)==1)automatic=2;
    if(automatic){
        if(modes.auto_mode)return nonce==modes.auto_nonce&&peer==modes.auto_owner&&automatic==modes.auto_mode?0:-1;
        if(!nonce||robot_blocked(&job)||mode_transaction_busy()||persist_fault||regions_fault||serial_fd<0)return -1;
        if(automatic==1&&(!modes.config.track_known||!modes.config.track_mask))return -1;
        if(automatic==2&&!regions.mask)return -1;
        modes.auto_mode=automatic;modes.auto_owner=peer;modes.auto_nonce=nonce;modes.auto_until=ms()+900;
        modes.auto_waiting=1;modes.auto_latched=modes.auto_candidate=modes.auto_has_job=0;
        modes.auto_sample=modes.auto_since=modes.auto_quiet=modes.auto_cooldown=0;modes.error=0;
        log_event(automatic==1?"automatic_tracking_enabled":"automatic_grab_enabled");return 0;
    }
    if(sscanf(text,"auto-off %16llx %c",&nonce,&extra)==1){
        if(!nonce)return -1;
        /* An explicit OFF can always cease future automatic work. It cannot
         * interrupt a blocking grab already accepted by the controller. */
        mode_auto_clear();
        if(job.mode==ROBOT_MODE_TRACK&&mode_active()){
            modes.track_armed=0;modes.stop_requested=1;
            modes.control_nonce=nonce;modes.owner=peer;modes.lease_until=ms()+900;
        }
        log_event("automatic_off_no_new_grabs_current_segment_finishes");return 0;
    }
    int track=sscanf(text,"track-hold %16llx %c",&nonce,&extra)==1;
    int cycle=!track&&sscanf(text,"cycle-stop %16llx %c",&nonce,&extra)==1;
    if(track||cycle){
        if(!nonce||serial_fd<0||job.mode!=(track?ROBOT_MODE_TRACK:ROBOT_MODE_CYCLE)||
           (!robot_blocked(&job)&&!modes.cycle_active))return -1;
        if(nonce==job.nonce)return -1;
        if(nonce==modes.control_nonce&&(modes.stop_requested||modes.transaction==MT_TRACK_HOLD||
           modes.transaction==MT_CYCLE_STOP||modes.cycle_stopping))return 0;
        mode_auto_clear();modes.track_armed=0;modes.stop_requested=1;modes.cycle_stopping=cycle?1u:0u;
        if(job.phase==ROBOT_UNKNOWN)job.phase=ROBOT_WAITING;
        /* An explicit stop can resolve an unknown mode; it is never used on a
         * blocking GRAB and is sent once only, after the outstanding reply. */
        modes.control_nonce=nonce;modes.owner=peer;modes.lease_until=ms()+900;return 0;
    }
    return -1;
}
static int mode_receive(const struct robot_packet *p,uint64_t now){
    unsigned kind=modes.transaction;
    if(!kind||p->id!=modes.id||p->type!=modes.expect)return 0;
    if(kind==MT_CONFIG_TRACK||kind==MT_CONFIG_CYCLE){
        if(!robot_mode_config_reply(&modes.config,p)){
            modes.transaction=modes.inspect=0;modes.error=RM_BAD_CONFIG;return 1;}
        modes.transaction=0;modes.inspect=kind==MT_CONFIG_TRACK?2u:0u;
        printf("ROBOT_CONFIG_VALID type=%02x id=%u length=%u\n",p->type,p->id,p->length);fflush(stdout);
        log_event("config_reply_valid");
    } else if(kind==MT_CYCLE_STATUS){
        if(p->payload_size!=6||p->payload[0]>1||p->payload[1]>2||p->payload[2]>4||
           p->payload[3]!=job.value){mode_unknown(RM_REJECTED,"cycle_invalid_status");return 1;}
        unsigned count=robot_get16(p->payload+4);
        if(count<modes.cycle_completed||(job.value&&count>job.value)||
           (p->payload[0]&&p->payload[2]!=1)||(!p->payload[0]&&p->payload[2]==1)||
           (p->payload[2]==2&&(!job.value||count!=job.value))){
            mode_unknown(RM_REJECTED,"cycle_invalid_count");return 1;}
        modes.transaction=0;modes.cycle_active=p->payload[0];modes.cycle_stage=p->payload[1];
        modes.cycle_reason=p->payload[2];modes.cycle_completed=count;
        if(!modes.cycle_active){
            unsigned reason=modes.cycle_reason;
            modes.cycle_stopping=0;
            if(reason!=2)modes.settle_until=now+(modes.config.cycle_known?
                modes.config.cycle_move_ms+modes.config.cycle_dwell_ms+150:6150);
            if(reason==2||reason==3)mode_end(ROBOT_COMPLETE,RM_OK,reason==2?"cycle_count_complete":"cycle_following_actions_stopped");
            else mode_end(ROBOT_FAILED,reason==4?RM_LEASE_LOST:RM_REJECTED,"cycle_controller_stopped");
        }
    } else {
        if(p->payload_size!=1)return 0;
        unsigned ack=p->payload[0];modes.transaction=0;
        if(ack){
            modes.inspect=0;
            if(ack==3){if(kind==MT_TRACK_START||kind==MT_TRACK_REFRESH)modes.config.track_known=0;
                if(kind==MT_CYCLE_START)modes.config.cycle_known=modes.config.cycle_ready=0;}
            if(ack==5)mode_unknown(RM_CONFLICT,"controller_other_mode_busy");
            else if(kind==MT_TRACK_START||kind==MT_CYCLE_START)
                mode_end(ROBOT_FAILED,ack==3?RM_UNCALIBRATED:RM_REJECTED,"mode_start_rejected");
            else mode_unknown(ack==3?RM_UNCALIBRATED:RM_REJECTED,"mode_request_rejected");
            return 1;
        }
        if(kind==MT_TRACK_HOLD){modes.track_region=0;modes.settle_until=now+1520;mode_end(ROBOT_COMPLETE,modes.error,"track_holding_current_segment_finishes");}
        else if(kind==MT_TRACK_START){modes.track_region=modes.value;job.phase=ROBOT_EXECUTING;modes.next_sample=now+200;}
        else if(kind==MT_TRACK_REFRESH){modes.track_region=modes.value;}
        else if(kind==MT_CYCLE_START){job.phase=ROBOT_EXECUTING;modes.next_sample=now;}
        else if(kind==MT_CYCLE_STOP){modes.cycle_stopping=1;job.phase=ROBOT_EXECUTING;modes.next_sample=now;}
    }
    feedback_seen=1;modes.last_reply=now;return 1;
}
static void mode_tick(uint64_t now){
    if(modes.transaction&&now>=modes.due){
        if(mode_active()||job.phase==ROBOT_UNKNOWN)mode_unknown(RM_TIMEOUT,"mode_timeout_no_retry");
        else {modes.transaction=modes.inspect=0;modes.error=RM_TIMEOUT;log_event("config_timeout_no_retry");}
        return;
    }
    if(mode_active()&&!modes.stop_requested&&
       (now>=modes.lease_until||modes.owner<=0||kill(modes.owner,0))){
        mode_unknown(RM_LEASE_LOST,"ui_lease_lost_heartbeats_ceased");return;
    }
    if(modes.transaction)return;
    if(modes.inspect){
        unsigned stage=modes.inspect;
        (void)mode_issue(stage==1?MT_CONFIG_TRACK:MT_CONFIG_CYCLE,stage==1?0x34:0x44,0,
                         stage==1?0x26:0x28,mode_allocate_id(),now);return;
    }
    if(modes.stop_requested){
        if(job.mode==ROBOT_MODE_TRACK){
            if(now-modes.last_track_wire<200)return;
            if(!mode_issue(MT_TRACK_HOLD,0x30,0,0x25,mode_allocate_id(),now))modes.stop_requested=0;
        }else if(job.mode==ROBOT_MODE_CYCLE){
            if(!mode_issue(MT_CYCLE_STOP,0x41,0,0x27,mode_allocate_id(),now))modes.stop_requested=0;
        }
        return;
    }
    if(!mode_active()||now<modes.next_sample)return;
    if(job.mode==ROBOT_MODE_TRACK&&modes.track_armed){
        if(now-modes.last_track_wire<200)return;
        struct panel_status s;unsigned target=0;
        if(current_status(&s))target=robot_track_observe(&modes.filter,&s,now);
        if(!target||!(modes.config.track_mask&(1u<<(target-1)))){
            modes.error=target?RM_UNCALIBRATED:RM_NO_TARGET;modes.track_armed=0;modes.stop_requested=1;return;}
        if(!mode_issue(MT_TRACK_REFRESH,0x30,target,0x25,mode_allocate_id(),now))modes.next_sample=now+200;
    }else if(job.mode==ROBOT_MODE_CYCLE){
        if(!mode_issue(MT_CYCLE_STATUS,0x47,0,0x29,mode_allocate_id(),now))modes.next_sample=now+300;
    }
}
static void mode_uart_lost(void){
    mode_auto_clear();
    modes.transaction=modes.inspect=modes.stop_requested=0;modes.track_armed=0;modes.lease_until=0;modes.owner=0;
    modes.config=(struct robot_mode_config){0};modes.error=RM_IO;
}
/* Automatic intent is independent of an individual workflow and survives a
 * quiet hold, but never a lease loss, UART fault, timeout or rejected motion. */
static void mode_auto_tick(uint64_t now){
    if(!modes.auto_mode)return;
    if(now>=modes.auto_until||modes.auto_owner<=0||kill(modes.auto_owner,0)){
        mode_auto_clear();modes.error=RM_LEASE_LOST;
        if(mode_active())mode_unknown(RM_LEASE_LOST,"automatic_lease_lost_no_resume");
        else log_event("automatic_lease_lost_no_resume");
        return;
    }
    if(persist_fault||regions_fault||serial_fd<0||job.phase==ROBOT_UNKNOWN||
       (modes.auto_has_job&&(job.phase==ROBOT_FAILED||job.phase==ROBOT_BUSY||job.phase==ROBOT_INVALID))){mode_auto_clear();return;}
    if(now<modes.auto_sample)return;
    modes.auto_sample=now+150;
    if(robot_blocked(&job)||mode_transaction_busy()){
        modes.auto_quiet=modes.auto_since=0;modes.auto_candidate=0;return;
    }
    struct panel_status s;int fresh=current_status(&s);
    if(fresh&&s.online&&s.acoustic_known&&!s.acoustic_enabled){
        mode_auto_clear();log_event("acoustic_disabled_automatic_off");return;}
    unsigned target=fresh?(modes.auto_mode==1?robot_track_region(&s):robot_target(&s)):0;
    modes.auto_waiting=1;
    if(modes.auto_mode==1){
        if(!target||!(modes.config.track_mask&(1u<<(target-1))))return;
        if(mode_start(ROBOT_MODE_TRACK,target,modes.auto_nonce,modes.auto_owner)){
            mode_auto_clear();return;}
        modes.auto_waiting=0;return;
    }
    /* Rearm only after an acknowledged workflow, then 2s of explicit fresh
     * QUIET. Moving/invalid/stale/disabled observations are not silence. */
    int quiet=fresh&&s.online&&s.acoustic_known&&s.acoustic_enabled&&s.enable_applied&&
        s.age_ms<=150&&!strcmp(s.sound,"quiet");
    if(modes.auto_latched){
        if(quiet){if(!modes.auto_quiet)modes.auto_quiet=now;
            if(now-modes.auto_quiet>=2000){modes.auto_latched=0;modes.auto_quiet=0;modes.auto_cooldown=now+3000;}}
        else modes.auto_quiet=0;
        return;
    }
    if(!target||!(regions.mask&(1u<<(target-1)))||now<modes.auto_cooldown){
        modes.auto_candidate=0;modes.auto_since=0;return;}
    if(target!=modes.auto_candidate){modes.auto_candidate=target;modes.auto_since=now;return;}
    if(now-modes.auto_since<1500)return;
    unsigned long long nonce=0;
    if(getrandom(&nonce,sizeof(nonce),GRND_NONBLOCK)!=(ssize_t)sizeof(nonce)||!nonce||nonce==job.nonce){mode_auto_clear();return;}
    modes.auto_latched=1;modes.auto_waiting=0;modes.auto_quiet=0;modes.auto_has_job=1;
    if(send_command(target,nonce,0))mode_auto_clear();
}
#endif
