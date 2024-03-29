#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include "opencv2/opencv.hpp"

using namespace cv;

typedef struct {
	int length;
	int seqNumber;
	int ackNumber;
	int fin;
	int syn;
	int ack;
} header;

typedef struct{
	header head;
	char data[1000];
} segment;

void setIP(char *dst, char *src) {
    if(strcmp(src, "0.0.0.0") == 0 || strcmp(src, "local") == 0
            || strcmp(src, "localhost")) {
        sscanf("127.0.0.1", "%s", dst);
    } else {
        sscanf(src, "%s", dst);
    }
}

int main(int argc, char* argv[])
{
    int sendersocket, portNum, nBytes;
    char videoname[1000];
    segment s_tmp;
    struct sockaddr_in sender, agent;
    socklen_t sender_size, agent_size;
    char ip[2][50];
    int port[2], i;

    if(argc != 6)
    {
        fprintf(stderr,"用法: %s <sender IP> <agent IP> <sender port> <agent port> <videoname>\n", argv[0]);
        fprintf(stderr, "例如: ./sender 127.0.0.1 127.0.0.1 8887 8888 tmp.mpg\n");
        exit(1);
    }
    else
    {
        setIP(ip[0], argv[1]);
        setIP(ip[1], argv[2]);

        sscanf(argv[3], "%d", &port[0]);
        sscanf(argv[4], "%d", &port[1]);

        sscanf(argv[5], "%s",videoname);
    }

    /*Create UDP socket*/
    sendersocket = socket(PF_INET, SOCK_DGRAM, 0);

    /*Configure settings in sender struct*/
    sender.sin_family = AF_INET;
    sender.sin_port = htons(port[0]);
    sender.sin_addr.s_addr = inet_addr(ip[0]);
    memset(sender.sin_zero, '\0', sizeof(sender.sin_zero));

    /*bind socket*/
    bind(sendersocket,(struct sockaddr *)&sender,sizeof(sender));

    /*Configure settings in agent struct*/
    agent.sin_family = AF_INET;
    agent.sin_port = htons(port[1]);
    agent.sin_addr.s_addr = inet_addr(ip[1]);
    memset(agent.sin_zero, '\0', sizeof(agent.sin_zero));

    /*Initialize size variable to be used later on*/
    sender_size = sizeof(sender);
    agent_size = sizeof(agent);

    int segment_size, index = 0;

    // server
    Mat imgServer;
    VideoCapture *cap = new VideoCapture(videoname);
    // get the resolution of the video
    int width = cap->get(CV_CAP_PROP_FRAME_WIDTH);
    int height = cap->get(CV_CAP_PROP_FRAME_HEIGHT);
    //tell client the resolution
    memset(&s_tmp, 0, sizeof(s_tmp));
    s_tmp.head.fin = 1;
    s_tmp.head.seqNumber = index;
    sprintf(s_tmp.data,"%d %d",width,height);
    while(1)
    {
        segment_size = sendto(sendersocket, &s_tmp, sizeof(segment), 0, (struct sockaddr *)&agent, agent_size);
        if(segment_size > 0)
        {
            printf("send	data	#%d\n",index);
            memset(&s_tmp, 0, sizeof(s_tmp));
            while(1)
            {
                segment_size = recvfrom(sendersocket, &s_tmp, sizeof(segment), 0, (struct sockaddr *)&agent, &agent_size);
                if(segment_size > 0)
                {
                    if(s_tmp.head.ackNumber == index)
                    {
                        printf("get     ack	#%d\n", index);
                        memset(&s_tmp, 0, sizeof(s_tmp));
                        index++;
                        break;
                    }
                }
            }
            break;
        }
    }
    //allocate container to load frames
    imgServer = Mat::zeros(height,width, CV_8UC3);
    // ensure the memory is continuous (for efficiency issue.)
    if(!imgServer.isContinuous())
    {
        imgServer = imgServer.clone();
    }

    while(1)
    {
        //get a frame from the video to the container on server.
        *cap >> imgServer;

        // get the size of a frame in bytes
        int imgSize = imgServer.total() * imgServer.elemSize();
        s_tmp.head.fin = 1;
        s_tmp.head.seqNumber = index;
        sprintf(s_tmp.data,"%d",imgSize);
        int resend = 0;
        while(1)
        {
            segment_size = sendto(sendersocket, &s_tmp, sizeof(segment), 0, (struct sockaddr *)&agent, agent_size);
            if(segment_size > 0)
            {
                printf("send	data	#%d\n",index);
                memset(&s_tmp, 0, sizeof(s_tmp));
                while(1)
                {
                    segment_size = recvfrom(sendersocket, &s_tmp, sizeof(segment), 0, (struct sockaddr *)&agent, &agent_size);
                    if(segment_size > 0)
                    {
                        if(s_tmp.head.ackNumber == index)
                        {
                            printf("get     ack	#%d\n", index);
                            memset(&s_tmp, 0, sizeof(s_tmp));
                            index++;
                            resend = 0;
                            break;
                        }
                        else
                        {
                            printf("get     ack	#%d\n", s_tmp.head.ackNumber);
                            memset(&s_tmp, 0, sizeof(s_tmp));
                            resend = 1;
                            break;
                        }
                    }
                }
                if(resend == 0)
                {
                    break;
                }
            }
        }

        // allocate a buffer to load the frame (there would be 2 buffers in the world of the Internet)
        uchar buffer[imgSize];

        // copy a frame to the buffer
        memcpy(buffer,imgServer.data, imgSize);

        int leftSize = imgSize;
        uchar *ptr = buffer;
        while(leftSize > 0)
        {
            s_tmp.head.seqNumber = index;
            memcpy(s_tmp.data,ptr,sizeof(s_tmp.data));
            int sentSize = sizeof(s_tmp.data);
            segment_size = sendto(sendersocket, &s_tmp, sizeof(segment), 0, (struct sockaddr *)&agent, agent_size);
            if(segment_size > 0)
            {
                printf("send	data	#%d\n",index);
                memset(&s_tmp, 0, sizeof(s_tmp));
                while(1)
                {
                    segment_size = recvfrom(sendersocket, &s_tmp, sizeof(segment), 0, (struct sockaddr *)&agent, &agent_size);
                    if(segment_size > 0)
                    {
                        if(s_tmp.head.ackNumber == index)
                        {
                            printf("get     ack	#%d\n", index);
                            memset(&s_tmp, 0, sizeof(s_tmp));
                            ptr += sentSize;
                            leftSize -= sentSize;
                            index++;
                            printf("%d\n",leftSize);
                            break;
                        }
                        else
                        {
                            printf("get     ack	#%d\n", s_tmp.head.ackNumber);
                            memset(&s_tmp, 0, sizeof(s_tmp));
                            break;
                        }
                    }
                }
            }
        }
        printf("send over\n");
        printf("%d\n",imgSize);
    }
    ////////////////////////////////////////////////////
	cap->release();
    return 0;
}