#include <stdio.h>
#include "v4l2_caf.h"
int main(void){
  printf("v4l2_buffer=%zu v4l2_plane=%zu v4l2_format=%zu v4l2_requestbuffers=%zu v4l2_capability=%zu sub=%zu\n",
    sizeof(struct v4l2_buffer), sizeof(struct v4l2_plane), sizeof(struct v4l2_format),
    sizeof(struct v4l2_requestbuffers), sizeof(struct v4l2_capability), sizeof(struct v4l2_event_subscription));
  printf("offsets in v4l2_buffer: m=%zu length=%zu\n",
    offsetof(struct v4l2_buffer, m), offsetof(struct v4l2_buffer, length));
  printf("ioctls: QBUF=0x%08x DQBUF=0x%08x S_FMT=0x%08x REQBUFS=0x%08x\n",
    VIDIOC_QBUF, VIDIOC_DQBUF, VIDIOC_S_FMT, VIDIOC_REQBUFS);
  return 0;
}
