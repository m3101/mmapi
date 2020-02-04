#include "mmapi_main.h"

//Copyright (c) 2020 Amélia O. F. da S. J.

int mmapi_create_device(char* path,mmapi_device **device)
{
    int ret;
    (*device)=malloc(sizeof(mmapi_device));
    if(!(*device))return NULL;
    if((ret=mmapi_start(*device,path))<0)
    {
        free(*device);
    }
    return ret;
}

int mmapi_available_names(char** names,char** paths,int len,int name_size,int path_size)
{
    DIR *idev=opendir("/dev/input");
    struct dirent *entry;
    int fd;
    int i=0;
    char path[path_size];
    if(idev)
    {
        while(entry=readdir(idev))
        {
            if(entry->d_type==DT_CHR)
            {
                strncpy(path,"/dev/input/",path_size);
                strncat(path,entry->d_name,path_size);
                fd=open(path,O_RDONLY|O_NONBLOCK);
                if(fd<=0)continue;
                ioctl(fd,EVIOCGNAME(name_size),names[i]);
                if(names[i][0]==0)strcpy(names[i],"Unknown");
                strncpy(paths[i],path,path_size);
                if(++i==len)break;
            }
        }
        closedir(idev);
        return i;
    }
    else return MMAPI_E_PATH;
}

int mmapi_start(mmapi_device *device,char *path)
{
    if(device&&path)
    {
        if(pipe(device->ctl)==-1)
            return MMAPI_E_PIPE;
        if(pipe(device->evt)==-1)
        {
            _mmapi_close_ctl(device);
            return MMAPI_E_PIPE;
        }
        device->fd=open(path, O_RDONLY|O_NONBLOCK);
        if(device->fd==-1)
        {
            _mmapi_close_ctl(device);
            _mmapi_close_evt(device);
            return MMAPI_E_PATH;
        }
        ioctl(device->fd,EVIOCGNAME(sizeof(device->name)),device->name);
        return mmapi_start_thread(device);
    }
    else
        return MMAPI_E_NULLPOINTER;
}

int mmapi_start_thread(mmapi_device *device)
{
    int f=fork();
    if(f>0)
    {
        close(device->ctl[0]);//Close read
        close(device->evt[1]);//Close write
        return 0;
    }
    else if(f<0)
        return f;
    else
    {
        struct input_event evt;
        unsigned int command;
        close(device->ctl[1]);//Close write
        close(device->evt[0]);//Close read
        while(1)
        {
            if(poll(&(struct pollfd){ .fd = device->ctl[0], .events = POLLIN }, 1, 0)==1)//If there is control data
            {
                command=read(device->ctl[0],(char*)command,sizeof(unsigned int));
                if(command&MMAPI_C_CLOSE)break;
            }
            if(read(device->fd,&evt,sizeof(evt))!=-1)
            {
                write(device->evt[1],&evt,sizeof(evt));
            }
        }
        return 0;
    }
}

struct input_event *mmapi_wait_adv(mmapi_device *device)
{
    struct input_event *evt=malloc(sizeof(struct input_event));
    read(device->evt[0],evt,sizeof(evt));
    return evt;
}

mmapi_event mmapi_wait(mmapi_device *device)
{
    struct input_event evt;
    int diff;
    read(device->evt[0],&evt,sizeof(evt));
    printf("Type: %hu, code: %hu, value: %i\n",evt.type,evt.code,evt.value);
    //These values were experimentally determined. Sorry.
    switch (evt.type)
    {
    case 1://Clicks
        switch(evt.code)
        {
        case 272://Left mouse button
            if(evt.value)
                return MMAPI_LCLICKDOWN;
            else return MMAPI_LCLICKUP;
            break;
        case 273://Right mouse button
            if(evt.value)
                return MMAPI_RCLICKDOWN;
            else return MMAPI_RCLICKUP;
            break;
        case 274://Middle mouse button
            if(evt.value)
                return MMAPI_MCLICKDOWN;
            else return MMAPI_MCLICKUP;
            break;
        case 330://Touchpad click start
            if(evt.value)
                return MMAPI_LCLICKDOWN;
            else return MMAPI_LCLICKUP;
            break;
        }
        break;
    case 2://Movement and Scrolling
        switch(evt.code)
        {
        case 0://Horizontal movement
            if(evt.value>0)
                return MMAPI_MOUSEMRIGHT;
            else if(evt.value<0)return MMAPI_MOUSEMLEFT;
            break;
        case 1://Vertical movement
            if(evt.value<0)
                return MMAPI_MOUSEMUP;
            else if(evt.value>0)return MMAPI_MOUSEMDOWN;
            break;
        case 8://Scrolling
            if(evt.value>0)
                return MMAPI_SCROLLUP;
            else return MMAPI_SCROLLDOWN;
        }
        break;
    case 3://Setting x/y (for trackpads only)
        switch (evt.code)//Bottom right edge seems to be the maximum value. Top left min.
        {
        case 53://x
            diff=evt.value-device->x;
            device->x=evt.value;
            if(diff>0) return MMAPI_MOUSEMRIGHT&MMAPI_UPDATEPOS;
            else if(diff<0) return MMAPI_MOUSEMLEFT&MMAPI_UPDATEPOS;
            else return MMAPI_UPDATEPOS;
            break;
        case 54://y
            diff=evt.value-device->y;
            device->y=evt.value;
            if(diff>0) return MMAPI_MOUSEMDOWN&MMAPI_UPDATEPOS;
            else if(diff<0) return MMAPI_MOUSEMUP&MMAPI_UPDATEPOS;
            else return MMAPI_UPDATEPOS;
            break;
        }
        break;
    }
    return 0;
}