//opencv lib
#include <cv.h>
#include <highgui.h>
//ardrone_tool_configuration is needed for the camera switch
#include <ardrone_tool/ardrone_tool_configuration.h>


//TODO: this if to send comand to the drone. You should move this where the drone threads are
//#include <ardrone_tool/UI/ardrone_input.h>

//NOTE: To make the drone take off
//ardrone_tool_set_ui_pad_start(1);
//ardrone_at_set_progress_cmd(0,0,0,0,0);

//NOTE: to make the drone land
//ardrone_at_set_progress_cmd(0,0,0,0,0);
//ardrone_tool_set_ui_pad_start(0);

// Extern so we can make the ardrone_tool_exit() function (ardrone_testing_tool.c)
// return TRUE when we close the video window
extern int exit_program;
extern int match;
extern int game_active;
extern int drone_score;
extern int enemy_score;
extern vp_os_mutex_t enemy_score_mutex;
extern vp_os_mutex_t drone_score_mutex;

//King of the Hill variables
//TODO: move this somewhere else, where it's easier to modify them
int MIN_H_HILL_1 = 0;
int MAX_H_HILL_1 = 15;
int MIN_S_HILL = 50;//150;
int MAX_S_HILL = 255;
int MIN_V_HILL = 15;
int MAX_V_HILL = 255;
int pixelRadius;
CvPoint coordinatesOfHillCenter;


//recognize a red ball up until now
IplImage* recognizeHills(IplImage* frame){
    
    //-----PHASE 1: DATA SETTING-----//
    
    //Create an HSV image on which we perform transformations and such
    IplImage* imgHSV = cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 3);
    cvCvtColor(frame, imgHSV, CV_RGB2HSV);
    
    
    //-----PHASE 2: THRESHOLDING AND COLOR RECOGNITION-----//
    
    //Threshold the image (i.e. black and white figure, with white being the object to detect)
    IplImage* imgThresholded = cvCreateImage(cvGetSize(imgHSV), IPL_DEPTH_8U, 1);
    IplImage* imgThresholded2 = cvCreateImage(cvGetSize(imgHSV), IPL_DEPTH_8U, 1);
    
    //To handle color wrap-around, two halves are detected and combined (for red color, otherwise we don't need this)
    cvInRangeS(imgHSV, cvScalar(MIN_H_HILL_1, MIN_S_HILL, MIN_V_HILL, 0), cvScalar(MAX_H_HILL_1, MAX_S_HILL, MAX_V_HILL, 0), imgThresholded); //red-yellow
    cvInRangeS(imgHSV, cvScalar(357, MIN_S_HILL,MIN_V_HILL, 0), cvScalar(360, MAX_S_HILL, MAX_V_HILL, 0), imgThresholded2); //magenta-red
    cvOr(imgThresholded, imgThresholded2, imgThresholded, 0);
    
    cvReleaseImage(&imgHSV);
    
    //-----PHASE 3: SHAPE DETECTION-----//
    
    CvMemStorage* storage = cvCreateMemStorage(0);
    
    //TODO: trying to filter some noise out. This has to be improved!
    cvErode(imgThresholded, imgThresholded, 0, 1);
    cvDilate(imgThresholded, imgThresholded, 0, 2);
    cvSmooth(imgThresholded, imgThresholded, CV_GAUSSIAN, 15, 15, 0, 0);
    
    //cvHoughCircles(source, circle storage, CV_HOUGH_GRADIENT, resolution, minDist, higher threshold, accumulator threshold, minRadius, maxRadius)
    //minDist = minimum distance between centers of neighborghood detected circles
    //accumulator threshold = at the center detection stage. The smaller it is, the more false circles may be detected.
    //minRadius = minimum radius of the circle to search for
    //maxRadius = max radius of the circles to search for. By default is set to max(image_width, image_height).
    //NOTE: The circles are stored from bigger to smaller.
    //TODO: improve this with live test!!
    CvSeq* circles = cvHoughCircles(imgThresholded, storage, CV_HOUGH_GRADIENT, 2, imgThresholded->height/4, 100, 100, 20, 200);
    
    
    //-----PHASE 4: CIRCLE DRAWING, BIGGEST CIRCLE DATA RETRIVAL AND DIMENSION UPDATING-----//
    
    int i;
    for (i = 0; i < circles->total; i++) {
        
        float* p = (float*)cvGetSeqElem( circles, i );
        
        //I pick only the first circle information because it's the biggest one == nearest
        /*if(i == 0){
         *           pixelRadius = cvRound(p[2]);
         *           coordinatesOfHillCenter = cvPoint(cvRound(p[0]), cvRound(p[1]));
    }*/
        
        //cvCircle(frame, center, radius, color, thickness, lineType, shift)
        cvCircle(frame, cvPoint(cvRound(p[0]),cvRound(p[1])), 3, CV_RGB(0,255,0), -1, 8, 0); //draw a circle
        cvCircle(frame, cvPoint(cvRound(p[0]),cvRound(p[1])), cvRound(p[2]), CV_RGB(255,0,0), 3, 8, 0);
    }
    
    //-----PHASE 5: FREEING MEMORY-----//
    
    //cvReleaseImage(&imgThresholded);
    cvReleaseImage(&imgThresholded2);
    
    cvClearSeq(circles);
    cvClearMemStorage(storage);
    cvReleaseMemStorage(&storage);
    
    return imgThresholded;
}


//TODO: I should put this inside another thread
void show_gui(uint8_t* frame){
    IplImage *img = cvCreateImageHeader(cvSize(640, 360), IPL_DEPTH_8U, 3);
    img->imageData = frame;
    
    IplImage *bla = recognizeHills(img);
    
    //This is to do after the hill and enemy detection because otherwise they won't work
    cvCvtColor(img, img, CV_BGR2RGB);
    
    CvFont font;
    cvInitFont(&font, CV_FONT_HERSHEY_SIMPLEX, 1.0f, 1.0f, 0, 1, CV_AA);
    
    //---- THIS PRINT THE SCORE OVER THE VIDEO ----//
    char drone_score_label[16] = "Drone score: "; //You have to create this string big enough to add the score, otherwise...buffer overflow!!
    char enemy_score_label[17] = "Player score: "; //Also, this
    char enemy_score_value[3];
    char drone_score_value[3];
    
    vp_os_mutex_lock(&drone_score_mutex);
        sprintf(drone_score_value, "%i", drone_score);
    vp_os_mutex_unlock(&drone_score_mutex);
    
    vp_os_mutex_lock(&enemy_score_mutex);
        sprintf(enemy_score_value, "%i", enemy_score);
    vp_os_mutex_unlock(&enemy_score_mutex);
    
    strcat(drone_score_label, drone_score_value);
    strcat(enemy_score_label, enemy_score_value);
    
    cvPutText(img, drone_score_label, cvPoint(30,30), &font, CV_RGB(255,0,0));
    cvPutText(img, enemy_score_label, cvPoint(350,30), &font, CV_RGB(0,255,0));
    
    //cvNamedWindow("video", CV_WINDOW_AUTOSIZE); //this will show a blank window!!!
    cvShowImage("Video", img);
    cvShowImage("Thresh", bla);
    
    int keyboard_input = cvWaitKey(1); //we wait 20ms and if something is pressed during this time, it 'goes' in c
    ZAP_VIDEO_CHANNEL channel = ZAP_CHANNEL_NEXT;
    
    //TODO: add every case that you need
    switch((char)keyboard_input){
        case 27://esc

            printf("The program will shutdown...\n");
            
            match = 0; //This tell the drone_logic thread to land the drone
            game_active = 0; //This make all the threads exit the while loop
            
            exit_program = 0;  // Force ardrone_tool to close
            // Sometimes, ardrone_tool might not finish properly. 
            //This happens mainly because a thread is blocked on a syscall, in this case, wait 5 seconds then kill the app
            sleep(5);
            exit(0);
            
            break;
        case 108: //l, as in land
            //TODO: this is here only for test purpose, should be removed before completion
            //ardrone_at_set_progress_cmd(0,0,0,0,0);
            //ardrone_tool_set_ui_pad_start(0);
            break;
        case 115: //s, as start match
            match = 1;
            //TODO: set the take_off variables to 1, maybe the match variable is enough
            break;
        case 116: //t, as in take off
            //TODO: this is here only for test purpose, should be removed before completion
            //ardrone_tool_set_ui_pad_start(1);
            //ardrone_at_set_progress_cmd(0,0,0,0,0);
        case 122: //z, as in zap channel
            //TODO: this is here only for test purpose, should be removed before completion
            ARDRONE_TOOL_CONFIGURATION_ADDEVENT(video_channel, &channel, NULL);
    }
    
    cvReleaseImage(&img);
}