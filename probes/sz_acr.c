#include <stdio.h>
#include <linux/videodev2.h>
int main(void){
  printf("v4l2_event=%zu v4l2_buffer=%zu\n", sizeof(struct v4l2_event), sizeof(struct v4l2_buffer));
  printf("VIDIOC_DQEVENT=0x%lx\n", (unsigned long)VIDIOC_DQEVENT);
  return 0;
}
