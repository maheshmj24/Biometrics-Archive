/**********************************************************************************************
This code is part of the code supplied with the OpenCV Blueprints book.
It was written by Steven Puttemans, who can be contacted via steven.puttemans[at]kuleuven.be
***********************************************************************************************
Software for recognizing iris images over a database

USAGE
./iris_processing_batch.cpp

NOTE: if you want more explanation in the code, take a look at iris_processing.cpp
***********************************************************************************************/
#include <sstream>
#include <fstream>
#include <iomanip>

#include "opencv2/opencv.hpp"

using namespace std;
using namespace cv;

int main( int argc, const char** argv )
{
    // We know the names from CASIA database, so this code runs over the items and processes them all.
    // We then apply the iris unwrapping for all of them, and store the results to train our matcher.
    vector<string> filenames;
    for(int number=1; number < 11; number++){
        stringstream filename_L_sample, filename_R_sample;
        filename_L_sample << "/data/iris/S1001L" << std::setfill('0') << std::setw(2) << number << ".jpg";
        filenames.push_back(filename_L_sample.str());
        filename_R_sample << "/data/iris/S1001R" << std::setfill('0') << std::setw(2) << number << ".jpg";
        filenames.push_back(filename_R_sample.str());
    }

    for(int counter=0; counter < filenames.size(); counter++){
        Mat original = imread(filenames[counter], IMREAD_GRAYSCALE);

        // ---------------------------------
        // STEP 1: segmentation of the pupil
        // ---------------------------------
        Mat mask_pupil;
        inRange(original, Scalar(30,30,30), Scalar(80,80,80), mask_pupil);
        vector< vector<Point> > contours;
        findContours(mask_pupil.clone(), contours, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_NONE);
        // Calculate all the corresponding areas which are larger than a certain area
        // This helps us remove small noise areas that still yield a contour
        vector< vector<Point> > filtered;
        for(size_t i=0; i<contours.size(); i++){
           double area = contourArea(contours[i]);
           // Remove noisy regions
           if(area > 50.0){
              filtered.push_back(contours[i]);
           }
        }
        // Now make a last check, if there are still multiple contours left, take the one that has a center closest to the image center
        vector<Point> final_contour=filtered[0];
        if(filtered.size() > 1){
           double distance = 5000;
           int index = -1;
           Point2f orig_center(original.cols/2, original.rows/2);
           for(size_t i=0; i<filtered.size(); i++){
              Moments temp = moments(filtered[i]);
              Point2f current_center((temp.m10/temp.m00), (temp.m01/temp.m00));
              // Find the Euclidean distance between both positions
              double dist = norm(Mat(orig_center), Mat(current_center));
              if(dist < distance){
                 distance = dist;
                 index = i;
              }
           }
           final_contour = filtered[index];
        }
        // Now finally make the black contoured image;
        vector< vector<Point> > draw;
        draw.push_back(final_contour);
        Mat blacked_pupil = original.clone();
        drawContours(blacked_pupil, draw, -1, Scalar(0,0,0), CV_FILLED);

        // We need to calculate the centroid
        // This centroid will be used to align the inner and outer contour
        Moments mu = moments(final_contour, true);
        Point2f centroid(mu.m10/mu.m00, mu.m01/mu.m00);

        // -----------------------------------
        // STEP 2: find the iris outer contour
        // -----------------------------------
        // Detect iris outer border
        // Apply a canny edge filter to look for borders
        // Then clean it a bit by adding a smoothing filter, reducing noise
        Mat blacked_canny, preprocessed;
        Canny(blacked_pupil, blacked_canny, 5, 70, 3);
        GaussianBlur(blacked_canny, preprocessed, Size(7,7), 0, 0);

        // Now run a set of HoughCircle detections with different parameters
        // We increase the second accumulator value until a single circle is left and take that one for granted
        int i = 80;
        Vec3f found_circle;
        while (i < 151){
           vector< Vec3f > storage;
           // If you use other data than the database provided, tweaking of these parameters will be neccesary
           HoughCircles(preprocessed, storage, CV_HOUGH_GRADIENT, 2, 100.0, 30, i, 100, 140);
           if(storage.size() == 1){
              found_circle = storage[0];
              break;
           }
           i++;
        }

        // -----------------------------------
        // STEP 3: make the final masked image
        // -----------------------------------
        Mat mask = Mat::zeros(blacked_pupil.rows, blacked_pupil.cols, CV_8UC1);
        circle(mask, centroid, found_circle[2], Scalar(255,255,255), CV_FILLED);
        Mat final_result = Mat::zeros(blacked_pupil.rows, blacked_pupil.cols, CV_8UC1);
        blacked_pupil.copyTo(final_result, mask);

        // --------------------------------------
        // STEP 4: cropping and radial unwrapping
        // --------------------------------------

        // Logpolar unwrapping
        // Lets first crop the final iris region from the image
        int radius = found_circle[2];
        int x = int(centroid.x - radius);
        int y = int(centroid.y - radius);
        // Add 2 elements to avoid information of the iris to be cut due to rounding errors
        int w = int(radius * 2) + 2;
        int h = w;
        Mat cropped_region = final_result( Rect(x,y,w,h) ).clone();

        // Now perform the unwrapping
        // This is done by the logpolar function who does Logpolar to Cartesian coordinates, so that it can get unwrapped properly
        Mat unwrapped;
        Point2f center (float(cropped_region.cols/2.0), float(cropped_region.cols /2.0));
        logPolar(cropped_region, unwrapped, center, 40, INTER_LINEAR +  WARP_FILL_OUTLIERS);

        // Make sure that we only get the region of interest
        // We do not need the black area for comparing
        Mat thresholded;
        // Apply some thresholding so that you keep a white blob where the eye pixels are
        threshold(unwrapped, thresholded, 10, 255, THRESH_BINARY);
        // Run a contour finding algorithm to locate the iris pixels
        // Then define the bounding box
        findContours(thresholded.clone(), contours, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_NONE);
        // Use the bounding box as the ROI for cutting off the black parts
        Rect ROI = boundingRect(contours[0]);
        Mat iris_pixels = unwrapped(ROI).clone();

        cerr << "*";

        // --------------------------------------
        // STEP 5: store the unwrapped iris image
        // --------------------------------------
        // Lets retrieve the filename and extension
        string filename = filenames[counter];
        stringstream temp(filename);
        string item;
        while( getline(temp, item, '/') ){
        }

        stringstream temp2;
        temp2 << "/data/iris_unwrapped/" << item;
        imwrite(temp2.str(), iris_pixels);

    }

    return 0;
}


