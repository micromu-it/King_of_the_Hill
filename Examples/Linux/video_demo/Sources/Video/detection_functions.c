//opencv lib
#include <cv.h>
#include <highgui.h>
//ardrone_tool_configuration is needed for the camera switch
#include <ardrone_tool/ardrone_tool_configuration.h>


//TODO: this if to send comand to the drone. You should move this where the drone threads are
#include <ardrone_tool/UI/ardrone_input.h>

//NOTE: To make the drone take off
//ardrone_tool_set_ui_pad_start(1);
//ardrone_at_set_progress_cmd(0,0,0,0,0);

//NOTE: to make the drone land
//ardrone_at_set_progress_cmd(0,0,0,0,0);
//ardrone_tool_set_ui_pad_start(0);

// Extern so we can make the ardrone_tool_exit() function (ardrone_testing_tool.c)
// return TRUE when we close the video window
extern int exit_program;

extern int debugging;

//King of the Hill variables
extern int match;
extern int game_active;
extern int drone_score;
extern int enemy_score;
extern vp_os_mutex_t enemy_score_mutex;
extern vp_os_mutex_t drone_score_mutex;

extern int hill_distance;
extern int enemy_distance;
extern int hill_offset_from_center;
extern int enemy_offset_from_center;
static const int POINT_OF_FOCUS = 555;
static const int HILL_REAL_RADIUS = 20; //TODO:This has to be checked!!
static const int ENEMY_REAL_HEIGHT = 35; //TODO: This has to be checked!!
static const int IMAGE_WIDTH = 640;

//Yellow Baloon
int MIN_H_HILL = 15;
int MAX_H_HILL = 90;
int MIN_S_HILL = 150;
int MAX_S_HILL = 255;
int MIN_V_HILL = 15;
int MAX_V_HILL = 255;
int pixel_radius;
CvPoint coordinatesOfHillCenter;

int MIN_H_ENEMY = 60; //100
int MAX_H_ENEMY = 120; //270
int MIN_S_ENEMY = 150; //TODO: this need to be calibrated with the material used (different material have different light reflection)
int MAX_S_ENEMY = 255;
int MIN_V_ENEMY = 15;
int MAX_V_ENEMY = 255;
int pixel_height;
int minPixelHeightAllowed;
int minPixelAreaAllowed = 500; //TODO: this need to be calibrated 
CvRect tmp_rectangle;
CvRect enemy_rectangle;

//NOTE: this is just to test that the drone "see" the right things
IplImage* testingVision(IplImage* frame){
    
    IplImage* imgHSV = cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 3);
    cvCvtColor(frame, imgHSV, CV_RGB2HSV);
    
    IplImage* imgThresholded = cvCreateImage(cvGetSize(imgHSV), IPL_DEPTH_8U, 1);
    cvInRangeS(imgHSV, cvScalar(MIN_H_ENEMY, MIN_S_ENEMY, MIN_V_ENEMY, 0), cvScalar(MAX_H_ENEMY, MAX_S_ENEMY, MAX_V_ENEMY, 0), imgThresholded);
    
    cvReleaseImage(&imgHSV);
    
    return imgThresholded;
}

//Detect the hill and calc the distance from the hill
//NOTE: a really far object can be erroneously detected as a nearer one.
void recognizeHills(IplImage* frame){
    
    //-----PHASE 1: DATA SETTING-----//
    
    //Create an HSV image on which we perform transformations and such
    IplImage* imgHSV = cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 3);
    cvCvtColor(frame, imgHSV, CV_RGB2HSV);
    
    //-----PHASE 2: THRESHOLDING AND COLOR RECOGNITION-----//
    
    //Threshold the image (i.e. black and white figure, with white being the object to detect)
    IplImage* imgThresholded = cvCreateImage(cvGetSize(imgHSV), IPL_DEPTH_8U, 1);
    cvInRangeS(imgHSV, cvScalar(MIN_H_HILL, MIN_S_HILL, MIN_V_HILL, 0), cvScalar(MAX_H_HILL, MAX_S_HILL, MAX_V_HILL, 0), imgThresholded);
    
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
    //TODO: I don't think I need this for cycle because I will use just the biggest circle, hopes is the nearest
    //int i;
    //for (i = 0; i < circles->total; i++) {
        
        float* p = (float*)cvGetSeqElem( circles, /*i*/0 );
        
        //I pick only the first circle information because it's the biggest one == nearest
        //if(i == 0){
            //x = p[0], y = p[1], radius = p[2]
            pixel_radius = cvRound(p[2]);
            hill_offset_from_center = -1*((IMAGE_WIDTH/2) - cvRound(p[1])); //the -1* is needed so negative value denote that the hill is to the left of center
        //}
        
        //cvCircle(frame, center, radius, color, thickness, lineType, shift)
        cvCircle(frame, cvPoint(cvRound(p[0]),cvRound(p[1])), 3, CV_RGB(0,255,0), -1, 8, 0); //draw a circle
        cvCircle(frame, cvPoint(cvRound(p[0]),cvRound(p[1])), cvRound(p[2]), CV_RGB(255,0,0), 3, 8, 0);
    //}
    
    //-----PHASE 5: FREEING MEMORY-----//
    
    cvReleaseImage(&imgThresholded);
    
    cvClearSeq(circles);
    cvClearMemStorage(storage);
    cvReleaseMemStorage(&storage);
    
    //-----PHASE 6: DETECTING HILL DISTANCE-----//
    //NOTE: Distance = (pointOfFocus * objectRealSize)/objectApparentSize 
    //NOTE: the image is 640x360pixel
    if(pixel_radius != 0){
        //TODO: test this
        hill_distance = (POINT_OF_FOCUS * HILL_REAL_RADIUS) / pixel_radius; //this is expressed in cm
    }
    
}

//Search for the enemy in the current image
void recognizeEnemy(IplImage* frame){
    
    //-----PHASE 1: DATA SETTING-----//
    
    //Create an HSV image on which we perform transformations and such
    IplImage* imgHSV=cvCreateImage(cvGetSize(frame), IPL_DEPTH_8U, 3);
    cvCvtColor(frame, imgHSV, CV_RGB2HSV);
    
    //-----PHASE 2: THRESHOLDING AND COLOR RECOGNITION-----//
    
    //Threshold the image (i.e. black and white figure, with white being the object to detect)
    IplImage* imgThresholded = cvCreateImage(cvGetSize(frame), 8, 1);
    cvInRangeS(imgHSV, cvScalar(MIN_H_ENEMY, MIN_S_ENEMY, MIN_V_ENEMY, 0), cvScalar(MAX_H_ENEMY, MAX_S_ENEMY, MAX_V_ENEMY, 0), imgThresholded);
    
    //-----PHASE 3: SHAPE DETECTION AND RECTANGLE DRAWING-----//
    
    //TODO:This is here to help reduce the noise. Has to be improved!!
    cvDilate(imgThresholded, imgThresholded, 0, 3);
    
    CvSeq* contours = NULL;
    CvSeq* result = NULL;
    CvMemStorage *storage= cvCreateMemStorage(0);
    enemy_rectangle = cvRect(0,0,0,0);
    
    cvFindContours(imgThresholded, storage, &contours, sizeof(CvContour), CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE, cvPoint(0,0));
    
    if(contours != NULL){
        //cycle through all the contours discovered to find the quadrilaters
        while(contours) {
            result = cvApproxPoly(contours, sizeof(CvContour), storage, CV_POLY_APPROX_DP, cvContourPerimeter(contours)*0.02, 0);
            
            //If a contour has 4 points and has an area bigger than minPixelAreaAllowed it's an enemy
            //NOTE: create a rectangle that "circle" the founded points, it always stays parallel to the ground so, if the rectangle is not parallel, we may have big error with the dimention.
            if(result->total == 4 && (fabs(cvContourArea(result,CV_WHOLE_SEQ,0)) > minPixelAreaAllowed) && cvCheckContourConvexity(result)){
                
                tmp_rectangle = cvBoundingRect(result, 0);
                //We keep only the biggest one
                if((enemy_rectangle.width*enemy_rectangle.height) < (tmp_rectangle.width*tmp_rectangle.height)){
                    enemy_rectangle = tmp_rectangle;
                }
            }
            
            contours = contours->h_next;
        }
    }
    
    pixel_height = enemy_rectangle.height;
    
    //DRAW THE RECTANGLE
    cvRectangle(frame, cvPoint(enemy_rectangle.x, enemy_rectangle.y), cvPoint(enemy_rectangle.x + enemy_rectangle.width, enemy_rectangle.y + enemy_rectangle.height), cvScalar(0, 0, 255, 0), 2, 8, 0);            
    
    int center_of_the_rectangle = enemy_rectangle.x + (enemy_rectangle.width/2); //on the x axis
    enemy_offset_from_center = -1*((IMAGE_WIDTH/2) - center_of_the_rectangle); //the -1* is needed so negative value denote that the hill is to the left of center
    
    
    //-----PHASE 4: FREEING MEMORY-----//
    
    cvReleaseImage(&imgHSV);
    cvReleaseImage(&imgThresholded);
    
    if(contours != NULL){
        cvClearSeq(contours);
    }
    if(result != NULL){
        cvClearSeq(result);
    }
    cvClearMemStorage(storage);
    cvReleaseMemStorage(&storage);
    
    //-----PHASE 5: DETECTING HILL DISTANCE-----//
    
    //NOTE: Distance = (pointOfFocus * objectRealSize)/objectApparentSize 
    //NOTE: the image is 640x360pixel
    if(pixel_height != 0){
        //enemy_distance is in cm, +-5cm
        enemy_distance = (POINT_OF_FOCUS * ENEMY_REAL_HEIGHT) / pixel_height;
    }
}

void keyboard_command_attuator(int keyboard_input){
    ZAP_VIDEO_CHANNEL channel = ZAP_CHANNEL_NEXT;
    
    //TODO: add every case that you need
    switch(keyboard_input){
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
            ardrone_at_set_progress_cmd(0,0.0,0.0,0.0,0.0);
            ardrone_tool_set_ui_pad_start(0);
            break;
        case 115: //s, as start match
            match = 1;
            //TODO: set the take_off variables to 1 or maybe the match variable is enough
            break;
        case 116: //t, as in take off
            //TODO: this is here only for test purpose, should be removed before completion
            ardrone_tool_set_ui_pad_start(1);
            ardrone_at_set_progress_cmd(0,0.0,0.0,0.0,0.0);
            break;
        case 122: //z, as in zap channel
            //TODO: this is here only for test purpose, should be removed before completion
            ARDRONE_TOOL_CONFIGURATION_ADDEVENT(video_channel, &channel, NULL);
            break;
    }
}

//TODO: I should put this inside another thread
void show_gui(uint8_t* frame){
    IplImage *img = cvCreateImageHeader(cvSize(640, 360), IPL_DEPTH_8U, 3);
    img->imageData = (char*)frame;
    
    if(debugging){
        IplImage *bla = testingVision(img);
        cvShowImage("Thresh", bla);
        cvReleaseImage(&bla);
    }
    
    recognizeEnemy(img);
    
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
    
    int keyboard_input = cvWaitKey(1); //we wait 20ms and if something is pressed during this time, it 'goes' in c
    keyboard_command_attuator(keyboard_input);
    
    cvReleaseImage(&img);
}