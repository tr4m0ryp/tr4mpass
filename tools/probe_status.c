#include <stdio.h>
#include <libirecovery.h>
int main(void){
    irecv_client_t c=NULL;
    if(irecv_open_with_ecid(&c,0)!=IRECV_E_SUCCESS){fprintf(stderr,"no dev\n");return 1;}
    unsigned char st[6]={0};
    int r=irecv_usb_control_transfer(c,0xA1,3,0,0,st,6,5000);
    fprintf(stderr,"GETSTATUS r=%d state=%u\n",r,r>0?st[4]:0);
    // DFU_CLRSTATUS
    int r2=irecv_usb_control_transfer(c,0x21,4,0,0,NULL,0,5000);
    fprintf(stderr,"CLRSTATUS r=%d\n",r2);
    irecv_close(c);
    return 0;
}
