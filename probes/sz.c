#include <stdio.h>
#include <linux/videodev2.h>
int main(void){
  printf("v4l2_event=%zu v4l2_capability=%zu v4l2_format=%zu v4l2_buffer=%zu v4l2_plane=%zu\n",
    sizeof(struct v4l2_event), sizeof(struct v4l2_capability),
    sizeof(struct v4l2_format), sizeof(struct v4l2_buffer), sizeof(struct v4l2_plane));
  printf("VIDIOC_DQEVENT=0x%lx SUBSCRIBE=0x%lx QUERYCAP=0x%lx\n",
    (unsigned long)VIDIOC_DQEVENT, (unsigned long)VIDIOC_SUBSCRIBE_EVENT,
    (unsigned long)VIDIOC_QUERYCAP);
  return 0;
}
