/*
*   UserInput.h     
*
*   Usage:  A touch event broker that tracks touch point events and uses time, distance, and number of touch points
*           to determine if an event is a button press, a swipe or a pinch activity.  Data is stored in Touch_Control structure.
*           
*           Button: 1 touch point < BUTTON_TOUCH max travel. It passes on the last X and Y coordinates.
*
*           Gesture: 1 or 2 touch points exceeding BUTTON_TOUCH minimum travel. It passes the distance to the gesture handler
*           Direction is positive or negatiove distance fopr X and Y.
*           
*           Non_Blocking: This is a non-blocking state engine with timer "gesture_timer" set for max press duration. 
*           The event is reset after timer expiration.  When a finger is eventually lifted it can become a valid press
*           or gesture again but with new start points. See Dragging description for exception.
*           
*           Dragging: The starting x,y coordinate are stored and a timer started.  While waiting for a press event to complete
*           the current x, y coordinates are updated to permit feedback for dragging type events such as operating a slider.  
*           The starting coordinates are not reset so the total drag distance can still be determined even though the timer has expired.
*
*           Number of touch points can be up to 5 but the code is only working with 2 here.
*/
#include <Metro.h>

#define BUTTON_TOUCH    40  // distance in pixels that defines a button vs a gesture. A drag and gesture will be > this value.
//#define MAXTOUCHLIMIT    2  //1...5

Metro gesture_timer=Metro(700);  // Change this to tune the button press timing. A drag will be > than this time.
extern Metro popup_timer; // used to check for popup screen request

// Our  extern declarations. Mostly needed for button activities.
//extern RA8875   tft;
//extern int16_t spectrum_preset;   // Specify the default layout option for spectrum window placement and size.
extern void RampVolume(float vol, int16_t rampType);
//extern void Spectrum_Parm_Generator(int);
//extern struct Spectrum_Parms Sp_Parms_Def[];
extern uint8_t curr_band;   // global tracks our current band setting.  
extern uint32_t VFOA;  // 0 value should never be used more than 1st boot before EEPROM since init should read last used from table.
extern uint32_t VFOB;
extern struct Band_Memory bandmem[];
extern struct User_Settings user_settings[];
extern struct Bandwidth_Settings bw[];
extern uint8_t user_Profile;
extern AudioControlSGTL5000 codec1;
extern uint8_t popup;

// Function declarations
void Button_Handler(int16_t x, uint16_t y); 
void Gesture_Handler(uint8_t gesture);

// structure to record the touch event info used to determine if there is a button press or a gesture.
struct Touch_Control{            
    //uint32_t  touch_start;  // using metro timer instead
    //uint32_t  elapsed_time;  // recalc this each time it is read. Make=O for start of events
    uint16_t    start_coordinates[MAXTOUCHLIMIT][2]; // start of event location
    uint16_t    last_coordinates[MAXTOUCHLIMIT][2];   // updated to curent or end of event location
int16_t         distance[MAXTOUCHLIMIT][2];  // signed value used for direction.  5 touch points with X and Y values each.
} static touch_evt;   // create a static instance of the structure to remember between events

//
// _______________________________________ Touch() ____________________________
// 
// Broker for touch events.  Determines if there is a valid button press or gesture and calls the appropriate function
//
//      Input:  None.  Assumes the FT5206 touch controller was started in setup()
//     Output:  Calls Button_Handler() or Gesture_Handler()  
// 
void Touch( void)
{
    uint8_t current_touches = 0;
    static uint8_t previous_touch = 0;
    uint16_t x,y,i;
  
    if (tft.touched())
    {      
        x = y = 0;
        tft.updateTS();                      
        current_touches = tft.getTouches(); 
        //Serial.print("Gesture Register=");  // used to test if controller gesture detection really works. It does not do very well.
        //Serial.println(tft.getGesture());

        // Start a state engine.  There are 4 states to track here. 
        // 1. Invalid touch event (not pressed hard enough or long enough).  
        //          current_touches = 0 &&  previous_touch = 0.
        // 2. Valid touch event started, finger(s) in contact. 
        //          current_touches = 1 &&  previous_touch = 0
        //   2a. Store event time start and coordinates into a structure
        //   2b. Set previous_touch = 1.
        // 3. Valid Touch pending, finger(s) still in contact
        //          current_touches = 1 &&  previous_touch = 1
        //   3a. If a timeout occurs, toss the results and start over. Set previous_touch = 0.
        //   3b. If not timed out yet, return with value = distance from last change and time elapsed.
        // Exception: If a slider is active, then report movement to the calling functions so they can do real time adjustments.  
        //      Examples include tuning, volume up and down, brightness adjust, attenuation adjust and so on.
        // 4. Valid touch completed, finger(s) lifted
        //   4a. If only 1 touch, coordinates and have not moved far, it must be a button press, not a swipe.
        //   4b. If only 1 touch, coordinates have moved far enough, must be a swipe.
        //   4c. If 2 touches, coordinates have not moved far enough, then set previous_touches to 0 and return, false alarm.
        //   4d. If 2 touches, cordnates have moved far enough, now direction can be determined. It must be a pinch gesture.
        
        // STATE 1
        if (!current_touches && !previous_touch)
            return;  // nothing to do, nothing pending, invalid touch event. Try pressing longer and/or harder.
        // STATE 2
        if (current_touches && !previous_touch)
        {
            //   2.  A valid touch has started.
            //   2a. Store event time start (or reset timer) and coordinates into a structure
            //   2b. Set previous_touch = 1.                        
            previous_touch = current_touches;  // will be 1 for buttons, 2 for gestures
            tft.updateTS();    // get current facts         
            tft.getTScoordinates(touch_evt.start_coordinates);  // Store the starting coordinates into the structure
            tft.getTScoordinates(touch_evt.last_coordinates);  // Easy way to effectively zero out the last coordinates
            //touch_evt.distanceX = touch_evt.distanceY = 0; // reset distance to 0

            for (i = 0; i< current_touches; i++)   /// Debug info
            {
                x = touch_evt.start_coordinates[i][0];
                y = touch_evt.start_coordinates[i][1];
                //Serial.print("Touch START time="); Serial.print(touch_evt.touch_start); 
                Serial.print(" touch point#="); Serial.print(i);
                Serial.print(" x="); Serial.print(x);
                Serial.print(" y="); Serial.println(y);        
            }
            //touch_evt.touch_start = millis();
            //touch_evt.elapsed_time = 0;  // may not need this, use metro timer instead
            gesture_timer.reset();   // reset timer
            return;
        }
        // STATE 3
        if (current_touches && previous_touch)
        {
        //   3. Valid Touch pending, finger(s) still in contact
        //          current_touches = 1 &&  previous_touch = 1
        //   3a. If a timeout occurs, toss the results and start over. Set previous_touch = 0.
        //   3b. If not timed out yet, return with value = distance set from last change and time elapsed.
        // Exception: If a slider is active, then report movement to the calling functions so thaty may do ral time adjsutments.  
        //      Examples include tuning, volume up and down, brightness adjust, attenuation adjust and so on.
            
            // Update elapsed time
            if (gesture_timer.check() == 1)
            {
                previous_touch = 0;// Our timer has expired
                Serial.println("Touch Timer expired");
                return;  // can calc the distance once we hav a valid event;
            }
            tft.updateTS();             
            tft.getTScoordinates(touch_evt.last_coordinates);  // Update curent coordinates
            return;           
        }
        // STATE 4
        if (!current_touches && previous_touch)   // Finger lifted, previous_touch knows if one or 2 touch points
        {
        // 4. Valid touch completed, finger(s) lifted
        //   4a. If only 1 touch, coordinates and have not moved far, it must be a button press, not a swipe.
        //   4b. If only 1 touch, coordinates have moved far enough, must be a swipe.
        //   4c. If 2 touches, coordinates have not moved far enough, then set previous_touches to 0 and return, false alarm.
        //   4d. If 2 touches, cordnates have moved far enough, now direction can be determined. It must be a pinch gesture.           
            tft.updateTS();             
            tft.getTScoordinates(touch_evt.last_coordinates);  // Update curent coordinates
            /// If the coordinates have moved far enough, it is a gesture not a button press.  
            //  Store the disances for touch even 0 now.
            touch_evt.distance[0][0] = (touch_evt.last_coordinates[0][0] - touch_evt.start_coordinates[0][0]);
            #ifdef DBG_GESTUREA             
            Serial.print("Distance 1 x="); Serial.println(touch_evt.distance[0][0]); 
            #endif
            touch_evt.distance[0][1] = (touch_evt.last_coordinates[0][1] - touch_evt.start_coordinates[0][1]);
            #ifdef DBG_GESTUREA 
            Serial.print("Distance 1 y="); Serial.println(touch_evt.distance[0][1]); 
            #endif

            if (previous_touch == 1)   // A value of 2 is 2 touch points. For button & slide/drag, only 1 touch point is present
            {
                touch_evt.distance[1][0] = 0;   // zero out 2nd touch point
                touch_evt.distance[1][1] = 0;             
            }
            else   // populate the distances for touch point 2
            {
                touch_evt.distance[1][0] = (touch_evt.last_coordinates[1][0] - touch_evt.start_coordinates[1][0]);
                #ifdef DBG_GESTUREA 
                Serial.print("Distance 2 x="); Serial.println(touch_evt.distance[1][0]); 
                #endif
                touch_evt.distance[1][1] = (touch_evt.last_coordinates[1][1] - touch_evt.start_coordinates[1][1]);
                #ifdef DBG_GESTUREA 
                Serial.print("Distance 2 y="); Serial.println(touch_evt.distance[1][1]); 
                #endif
            }

            // if only 1 touch and X or Y distance is OK for a button call the button event handler with coordinates
            if (previous_touch == 1 && (abs(touch_evt.distance[0][0]) < BUTTON_TOUCH && abs(touch_evt.distance[0][1]) < BUTTON_TOUCH))
            {
                Button_Handler(touch_evt.start_coordinates[0][0],  touch_evt.start_coordinates[0][1]);  // pass X and Y
            }
            else  // Had 2 touches or 1 swipe touch - Distance was longer than a button touch so must be a swipe
            {   
                Gesture_Handler(previous_touch);   // moved enough to be a gesture
            }
            previous_touch = 0;   // Done, reset this for a new event
            touch_evt.distance[0][0] = 0;   // zero out 1st touch point
            touch_evt.distance[0][1] = 0;       
            touch_evt.distance[1][0] = 0;   // zero out 2nd touch point
            touch_evt.distance[1][1] = 0;       
        }   // End State 4
    }
}
//
// _______________________________________ Gesture_Handler ____________________________________
//
/*
*   The built in gesture detection rarely works.
*   Only pinch and swipe up are ever reported on my test display and swipe up is a rare event.
*   So we will track the touch point time and coordinates and figure it out on our own.
*
*/
void Gesture_Handler(uint8_t gesture)
{
    // Get our various coordinates
    int16_t T1_X = touch_evt.distance[0][0];  
    int16_t T1_Y = touch_evt.distance[0][1];   
    int16_t t1_x_s = touch_evt.start_coordinates[0][0];
    int16_t t1_y_s = touch_evt.start_coordinates[0][1];
    int16_t t2_x_s = touch_evt.start_coordinates[1][0];
    int16_t t2_y_s = touch_evt.start_coordinates[1][1];

    int16_t t1_x_e = touch_evt.last_coordinates[0][0];
    int16_t t1_y_e = touch_evt.last_coordinates[0][1];
    int16_t t2_x_e = touch_evt.last_coordinates[1][0];
    int16_t t2_y_e = touch_evt.last_coordinates[1][1];

    switch (gesture) 
    {
        ////------------------ SWIPE -------------------------------------------
        case 1:  // only 1 touch so must be a swipe or drag.  Get direction vertical or horizontal
        { 
            //#define DBG_GESTURE

            #ifdef DBG_GESTURE
            Serial.print(" T1_X="); Serial.print(T1_X);
            Serial.print(" T1_Y="); Serial.print(T1_Y);                
            #endif

            ////------------------ SWIPE ----------------------------------------------------////
            if ( abs(T1_Y) > abs(T1_X)) // Y moved, not X, vertical swipe    
            {               
                //Serial.println("\nSwipe Vertical");
                ////------------------ SWIPE DOWN  -------------------------------------------
                if (T1_Y > 0)  // y is negative so must be vertical swipe down direction                    
                {                    
                    //Serial.println(" Swipe DOWN"); 
                    //Serial.println("Band -");
                    changeBands(-1);                                     
                } 
                ////------------------ SWIPE UP  -------------------------------------------
                else
                {
                    //Serial.println(" Swipe UP");
                    //Set_Spectrum_RefLvl(1);   // Swipe up    
                    //Serial.println("Band +");
                    changeBands(1);                                     
                }
            } 
            ////------------------ SWIPE LEFT & RIGHT -------------------------------------------////
            else  // X moved, not Y, horizontal swipe
            {
                ////------------------ SWIPE LEFT  -------------------------------------------
                //Serial.println("\nSwipe Horizontal");
                if (T1_X < 0)  // x is smaller so must be swipe left direction
                {                
                    Rate(-1);
                    Serial.println("Swiped Left");                                           
                    return; 
                }
                ////------------------ SWIPE RIGHT  -------------------------------------------
                else  // or larger so a Swipe Right
                {
                    Rate(1);
                    Serial.println("Swiped Right");             
                    return;    
                }
            }                                 
            break;
        }
        ////------------------ PINCH -------------------------------------------
        case 2: // look for T1 going in opposite directiom compared to T2. If T1-T2 closer to 0 it is a Pinch IN            
        {       
            // Calculate the distance between T1 and T2 at the start
            int16_t dist_start  = sqrt(pow(t2_x_s - t1_x_s, 2) + pow(t2_y_s - t1_y_s, 2));
            int16_t dist_end    = sqrt(pow(t2_x_e - t1_x_e, 2) + pow(t2_y_e - t1_y_e, 2));
            #ifdef DBG_GESTURE
            Serial.print("Dist Start ="); Serial.println(dist_start);
            Serial.print("Dist End   ="); Serial.println(dist_end);
            #endif
            // Calculate the distance between T1 and T2 at the end   
            if (dist_start - dist_end > 200 )                        
                Set_Spectrum_Scale(-1); // Was a pinch in.  Just pass on direction, the end function can look for distance if needed
            if (dist_end - dist_start > 200)                        
                Set_Spectrum_Scale(1); // was a pinch out   
            if (dist_end - dist_start <= 200  && abs(t1_x_s - t1_x_e) < 200  && abs(t1_y_s - t1_y_e) > 200)
            {
                Serial.println("Volume UP");
                //codec1.volume(bandmem[0].spkr_Vol_last+0.2);  // was 2 finger swipe down
            }
            else if (dist_start - dist_end <= 200)
            {
                Serial.println("Volume DOWN");
                //codec1.volume(bandmem[0].spkr_Vol_last-0.2);  // was 2 finger swipe up
            }
            break;
        }
        case 3: if (abs(T1_Y) > abs(T1_X)) // Y moved, not X, vertical swipe    
        {               
            //Serial.println("\n3 point swipe Vertical");
            ////------------------ SWIPE DOWN  -------------------------------------------
            if (T1_Y > 0)  // y is negative so must be vertical swipe do
            {                
                codec1.volume(user_settings[user_Profile].spkr_Vol_last -= 0.2);  // was 3 finger swipe down
                
                Serial.print("3-point Volume DOWN  "); Serial.println(user_settings[user_Profile].spkr_Vol_last);
            }
            else
            {
                codec1.volume(user_settings[user_Profile].spkr_Vol_last += 0.1);  // was 3 finger swipe up
                Serial.print("3-point Volume UP  "); Serial.println(user_settings[user_Profile].spkr_Vol_last);
            }                
            break;
        }        
        case 0: // nothing applicable, leave
       default: Serial.println(" Gesture = 0 : Should not be here!");
                return;  // leave, should not be here
    }
}
//
// _______________________________________ Button_Handler ____________________________
//
//   Input:     X and Y coordinates for normal button push events.
//  Output:     None.  Acts on specified button.  
//   Usage:     Called from the touch sensor broker Touch().
//
//      Enabled:
//      The enabled.  It was intended to track the state of a button.  In reality most buttons and labels 
//      read direct from another table to determine the feature state and then update to match or most often 
//      is ignored/not used.  There are some cases where a button is all that exists, usually as a placeholder
//      for a future feature so the button may store its own on/off state until the real feature arrives
//      with a place to query that feature status.
//
//      Show:
//      Show is for button or label visiblity.  Must be on for a button or label to be visible on screen
//      In the case of panels of buttons that alternately show and hide, a touch event can be for any of 
//      several buttons occupying the same space. Use the show property to control when to show a button
//      and only pass through touch event to the control function when the button or label is showing.
//      Show applies only to the touch system processing. 
//      Other controllers can call the control function bypassing the touch system.
//      Display functions only report teh eisxting state and do not alter states.  They can update a button
//      text info or label color to match the current state when the object is showing.
//    
//      Use:
//      So how does a touch event get processed here?
//      1. Touch determines it is a 1 point non drag event and passes the X,Y coordinates to a list
//      of XY rectangle definitions here.
//      2. Only 1 object is visible in the touch space at any moment
//      3. Out of sight buttons must be ignoere by a touch event.
//      4. A non touch event can call the same cointrol fucntion teh toch event does.
//      5. Normally the control event launches a display update calling any linked objects.  It know best when
//      something has changed.
//      5. The Display functions get called in all cases of change but must check the show status and exit if the object is
//      currently hidden.  It may update an active label but not update a hidden button.
//      6. When a lavble or button is first drawn (rotate from hidden to show state) it will refresh its status live.
//      7.  A multifintion knob or panel switch or remote command may call a control function and no touch involved.
//      The control and display fucntions must proceed.
//  
void Button_Handler(int16_t x, uint16_t y)
{
    Serial.print("Button:");Serial.print(x);Serial.print(" ");Serial.println(y);
    
    struct Standard_Button *ptr = std_btn; // pointer to standard button layout table
    struct Label *pLabel = labels;

    if (popup)
        popup_timer.reset();

    // MODE button
    ptr = std_btn + MODE_BTN;     // pointer to button object passed by calling function    
    if((x > ptr->bx && x < ptr->bx + ptr->bw) && ( y > ptr->by && y < ptr->by + ptr->bh))
        if (ptr->show) setMode(1);  //Increment the mode from current value
    // MODE Label
    pLabel = labels + MODE_LBL;
    if((x > pLabel->x && x < pLabel->x + pLabel->w) && ( y > pLabel->y && y < pLabel->y + pLabel->h))
        if (pLabel->show) setMode(1);  //Increment the mode from current value

    // FILTER button
    ptr = std_btn + FILTER_BTN;     // pointer to button object passed by calling function
    if ((x > ptr->bx && x < ptr->bx + ptr->bw) && ( y > ptr->by && y < ptr->by + ptr->bh))
       if (ptr->show) Filter(0); 
    // FILTER label
    pLabel = labels + FILTER_LBL;
    if ((x > pLabel->x && x < pLabel->x + pLabel->w) && ( y > pLabel->y && y < pLabel->y + pLabel->h))
        if (pLabel->show) Filter(0);  

    // RATE button
    ptr = std_btn + RATE_BTN;     // pointer to button object passed by calling function        
    if ((x > ptr->bx && x < ptr->bx + ptr->bw) && ( y > ptr->by && y < ptr->by + ptr->bh))
        if (ptr->show) Rate(0);     //Increment from current value   
    // RATE label
    pLabel = labels + RATE_LBL;
    if ((x > pLabel->x && x < pLabel->x + pLabel->w) && ( y > pLabel->y && y < pLabel->y + pLabel->h))
        if (pLabel->show) Rate(0);  //Increment from current value 

    // AGC button
    ptr = std_btn + AGC_BTN;     // pointer to button object passed by calling function
    if ((x > ptr->bx && x < ptr->bx + ptr->bw) && ( y > ptr->by && y < ptr->by + ptr->bh))
        if (ptr->show) AGC();
    // AGC label
    pLabel = labels + AGC_LBL;
    if ((x > pLabel->x && x < pLabel->x + pLabel->w) && ( y > pLabel->y && y < pLabel->y + pLabel->h))
        if (pLabel->show) AGC();

    // ANT button
    ptr = std_btn + ANT_BTN;     // pointer to button object passed by calling function
    if ((x > ptr->bx && x < ptr->bx + ptr->bw) && ( y > ptr->by && y < ptr->by + ptr->bh))
        if (ptr->show) Ant();
    // ANT label
    pLabel = labels + ANT_LBL;
    if ((x > pLabel->x && x < pLabel->x + pLabel->w) && ( y > pLabel->y && y < pLabel->y + pLabel->h))
        if (pLabel->show) Ant();

    // MUTE
    ptr = std_btn + MUTE_BTN;     // pointer to button object passed by calling function
    if ((x > ptr->bx && x < ptr->bx + ptr->bw) && ( y > ptr->by && y < ptr->by + ptr->bh))  
        if (ptr->show) Mute();

    // MENU button
    ptr = std_btn + MENU_BTN;     // pointer to button object passed by calling function
    if ((x > ptr->bx && x < ptr->bx + ptr->bw) && ( y > ptr->by && y < ptr->by + ptr->bh))
        if (ptr->show) Menu();

    // VFO A and B Switching button - Can touch the A/B button or the Frequency Label itself to toggle VFOs.
    ptr = std_btn + VFO_AB_BTN;     // pointer to button object passed by calling function
    if ((x > ptr->bx && x < ptr->bx + ptr->bw) && ( y > ptr->by && y < ptr->by + ptr->bh))
        if (ptr->show) VFO_AB();
        
    struct Frequency_Display *ptrAct  = &disp_Freq[0];   // The frequency display areas itself  
    if ((x > ptrAct->bx && x < ptrAct->bx + ptrAct->bw) && ( y > ptrAct->by && y < ptrAct->by + ptrAct->bh))
        VFO_AB();

    struct Frequency_Display *ptrStby = &disp_Freq[2];   // The frequency display areas itself  
    if ((x > ptrStby->bx && x < ptrStby->bx + ptrStby->bw) && ( y > ptrStby->by && y < ptrStby->by + ptrStby->bh))
        VFO_AB();
        
    // ATTENUATOR button
    ptr = std_btn + ATTEN_BTN;     // pointer to button object passed by calling function
    if ((x > ptr->bx && x < ptr->bx + ptr->bw) && ( y > ptr->by && y < ptr->by + ptr->bh))
        if (ptr->show) Atten(2);   // 2 = toggle state, 1 is set, 1 is off, -1 use current

    // PREAMP button
    ptr = std_btn + PREAMP_BTN;     // pointer to button object passed by calling function
    if ((x > ptr->bx && x < ptr->bx + ptr->bw) && ( y > ptr->by && y < ptr->by + ptr->bh))
        if (ptr->show) Preamp(2);  // 2 = toggle state, 1 is set, 1 is off, -1 use current

    // RIT button
    ptr = std_btn + RIT_BTN;     // pointer to button object passed by calling function
    if ((x > ptr->bx && x < ptr->bx + ptr->bw) && ( y > ptr->by && y < ptr->by + ptr->bh))
        if (ptr->show) RIT();
    
    // XIT button
    ptr = std_btn + XIT_BTN;     // pointer to button object passed by calling function
    if ((x > ptr->bx && x < ptr->bx + ptr->bw) && ( y > ptr->by && y < ptr->by + ptr->bh))
        if (ptr->show) XIT();
    
    // SPLIT button
    ptr = std_btn + SPLIT_BTN;     // pointer to button object passed by calling function
    if ((x > ptr->bx && x < ptr->bx + ptr->bw) && ( y > ptr->by && y < ptr->by + ptr->bh))
        if (ptr->show) Split();
    
    // XVTR button
    ptr = std_btn + XVTR_BTN;     // pointer to button object passed by calling function
    if ((x > ptr->bx && x < ptr->bx + ptr->bw) && ( y > ptr->by && y < ptr->by + ptr->bh))
        if (ptr->show) Xvtr();

    // ATU button
    ptr = std_btn + ATU_BTN;     // pointer to button object passed by calling function
    if ((x > ptr->bx && x < ptr->bx + ptr->bw) && ( y > ptr->by && y < ptr->by + ptr->bh))
        if (ptr->show) ATU();

    // Fine button
    ptr = std_btn + FINE_BTN;     // pointer to button object passed by calling function
    if ((x > ptr->bx && x < ptr->bx + ptr->bw) && ( y > ptr->by && y < ptr->by + ptr->bh))
        if (ptr->show) Fine();

    // XMIT button
    ptr = std_btn + XMIT_BTN;     // pointer to button object passed by calling function
    if ((x > ptr->bx && x < ptr->bx + ptr->bw) && ( y > ptr->by && y < ptr->by + ptr->bh))
        if (ptr->show) Xmit();

    // NB button
    ptr = std_btn + NB_BTN;     // pointer to button object passed by calling function
    if ((x > ptr->bx && x < ptr->bx + ptr->bw) && ( y > ptr->by && y < ptr->by + ptr->bh))
        if (ptr->show) NB();
     
    // NR button
    ptr = std_btn + NR_BTN;     // pointer to button object passed by calling function
    if ((x > ptr->bx && x < ptr->bx + ptr->bw) && ( y > ptr->by && y < ptr->by + ptr->bh))
       if (ptr->show)  NR();

    // Enet button
    ptr = std_btn + ENET_BTN;     // pointer to button object passed by calling function
    if ((x > ptr->bx && x < ptr->bx + ptr->bw) && ( y > ptr->by && y < ptr->by + ptr->bh))
        if (ptr->show) Enet();

    // Spot button
    ptr = std_btn + SPOT_BTN;     // pointer to button object passed by calling function
    if ((x > ptr->bx && x < ptr->bx + ptr->bw) && ( y > ptr->by && y < ptr->by + ptr->bh))
        if (ptr->show) Spot();

    // Notch button
    ptr = std_btn + NOTCH_BTN;     // pointer to button object passed by calling function
    if ((x > ptr->bx && x < ptr->bx + ptr->bw) && ( y > ptr->by && y < ptr->by + ptr->bh))
        if (ptr->show) Notch();

    // BAND UP button
    ptr = std_btn + BANDUP_BTN;     // pointer to button object passed by calling function
    if ((x > ptr->bx && x < ptr->bx + ptr->bw) && ( y > ptr->by && y < ptr->by + ptr->bh))
        if (ptr->show) BandUp();

    // BAND DOWN button
    ptr = std_btn + BANDDN_BTN;     // pointer to button object passed by calling function
    if ((x > ptr->bx && x < ptr->bx + ptr->bw) && ( y > ptr->by && y < ptr->by + ptr->bh))
        if (ptr->show) BandDn();
    
    // BAND button
    ptr = std_btn + BAND_BTN;     // pointer to button object passed by calling function
    if ((x > ptr->bx && x < ptr->bx + ptr->bw) && ( y > ptr->by && y < ptr->by + ptr->bh))
        if (ptr->show) Band();

    // DISPLAY button
    ptr = std_btn + DISPLAY_BTN;     // pointer to button object passed by calling function
    if ((x > ptr->bx && x < ptr->bx + ptr->bw) && ( y > ptr->by && y < ptr->by + ptr->bh))
        if (ptr->show) Display();

    // FN button  - Cycles out a list (panel) of buttons
    ptr = std_btn + FN_BTN;     // pointer to button object passed by calling function
    if ((x > ptr->bx && x < ptr->bx + ptr->bw) && ( y > ptr->by && y < ptr->by + ptr->bh))
    {
        std_btn[FN_BTN].enabled += 1;
        if (std_btn[FN_BTN].enabled > PANEL_ROWS-1)
            std_btn[FN_BTN].enabled = 2;

        if (1)  // ptr->enabled)   For now this button is always active.
        {   
            if (std_btn[FN_BTN].enabled == 3)   // Panel 2 buttons
            {
                strcpy(std_btn[FN_BTN].label, "Fn 2\0");
                // Turn OFF Panel 1 buttons
                std_btn[MODE_BTN].show    = OFF;
                std_btn[FILTER_BTN].show  = OFF;
                std_btn[ATTEN_BTN].show   = OFF;
                std_btn[PREAMP_BTN].show  = OFF;
                std_btn[RATE_BTN].show    = OFF;                
                std_btn[BAND_BTN].show    = OFF;
                // Turn ON Panel 2 buttons                
                std_btn[NB_BTN].show      = ON;
                std_btn[NR_BTN].show      = ON;
                std_btn[SPOT_BTN].show    = ON;
                std_btn[NOTCH_BTN].show   = ON;
                std_btn[AGC_BTN].show     = ON;
                std_btn[MUTE_BTN].show    = ON;
                // Turn OFF Panel 3 buttons
                std_btn[MENU_BTN].show    = OFF;
                std_btn[ANT_BTN].show     = OFF;
                std_btn[ATU_BTN].show     = OFF;
                std_btn[XMIT_BTN].show    = OFF;
                std_btn[BANDDN_BTN].show  = OFF;
                std_btn[BANDUP_BTN].show  = OFF;
                // Turn OFF Panel 4 buttons
                std_btn[RIT_BTN].show     = OFF;
                std_btn[XIT_BTN].show     = OFF;
                std_btn[VFO_AB_BTN].show  = OFF;
                std_btn[FINE_BTN].show    = OFF;
                std_btn[DISPLAY_BTN].show = OFF;
                std_btn[SPLIT_BTN].show   = OFF;
            }
            else if (std_btn[FN_BTN].enabled == 4)// Panel 3 buttons
            {           
                strcpy(std_btn[FN_BTN].label, "Fn 3\0");
                // Turn OFF Panel 1 buttons
                std_btn[MODE_BTN].show    = OFF;
                std_btn[FILTER_BTN].show  = OFF;
                std_btn[ATTEN_BTN].show   = OFF;
                std_btn[PREAMP_BTN].show  = OFF;
                std_btn[RATE_BTN].show    = OFF;                
                std_btn[BAND_BTN].show    = OFF;
                // Turn OFF Panel 2 buttons                
                std_btn[NB_BTN].show      = OFF;
                std_btn[NR_BTN].show      = OFF;
                std_btn[SPOT_BTN].show    = OFF;
                std_btn[NOTCH_BTN].show   = OFF;
                std_btn[AGC_BTN].show     = OFF;
                std_btn[MUTE_BTN].show    = OFF;
                // Turn ON Panel 3 buttons
                std_btn[MENU_BTN].show    = ON;
                std_btn[ANT_BTN].show     = ON;
                std_btn[ATU_BTN].show     = ON;
                std_btn[XMIT_BTN].show    = ON;
                std_btn[BANDDN_BTN].show  = ON;
                std_btn[BANDUP_BTN].show  = ON;
                // Turn OFF Panel 4 buttons
                std_btn[RIT_BTN].show     = OFF;
                std_btn[XIT_BTN].show     = OFF;
                std_btn[VFO_AB_BTN].show  = OFF;
                std_btn[FINE_BTN].show    = OFF;
                std_btn[DISPLAY_BTN].show = OFF;
                std_btn[SPLIT_BTN].show   = OFF;
            }
            else if (std_btn[FN_BTN].enabled == 5)// Panel 4 buttons
            {           
                strcpy(std_btn[FN_BTN].label, "Fn 4\0");
                // Turn OFF Panel 1 buttons
                std_btn[MODE_BTN].show    = OFF;
                std_btn[FILTER_BTN].show  = OFF;
                std_btn[ATTEN_BTN].show   = OFF;
                std_btn[PREAMP_BTN].show  = OFF;
                std_btn[RATE_BTN].show    = OFF;                
                std_btn[BAND_BTN].show    = OFF;
                // Turn OFF Panel 2 buttons                
                std_btn[NB_BTN].show      = OFF;
                std_btn[NR_BTN].show      = OFF;
                std_btn[SPOT_BTN].show    = OFF;
                std_btn[NOTCH_BTN].show   = OFF;
                std_btn[AGC_BTN].show     = OFF;
                std_btn[MUTE_BTN].show    = OFF;
                // Turn OFF Panel 3 buttons
                std_btn[MENU_BTN].show    = OFF;
                std_btn[ANT_BTN].show     = OFF;
                std_btn[ATU_BTN].show     = OFF;
                std_btn[XMIT_BTN].show    = OFF;
                std_btn[BANDDN_BTN].show  = OFF;
                std_btn[BANDUP_BTN].show  = OFF;
                // Turn ON Panel 4 buttons
                std_btn[RIT_BTN].show     = ON;
                std_btn[XIT_BTN].show     = ON;
                std_btn[VFO_AB_BTN].show  = ON;
                std_btn[FINE_BTN].show    = ON;
                std_btn[DISPLAY_BTN].show = ON;
                std_btn[SPLIT_BTN].show   = ON; 
            }
            else    // if (std_btn[FN_BTN].enabled == 2)
            {
                strcpy(std_btn[FN_BTN].label, "Fn 1\0");
                // Turn ON Panel 1 buttons
                std_btn[MODE_BTN].show    = ON;
                std_btn[FILTER_BTN].show  = ON;
                std_btn[ATTEN_BTN].show   = ON;
                std_btn[PREAMP_BTN].show  = ON;
                std_btn[RATE_BTN].show    = ON;                
                std_btn[BAND_BTN].show    = ON;
                // Turn OFF Panel 2 buttons                
                std_btn[NB_BTN].show      = OFF;
                std_btn[NR_BTN].show      = OFF;
                std_btn[SPOT_BTN].show    = OFF;
                std_btn[NOTCH_BTN].show   = OFF;
                std_btn[AGC_BTN].show     = OFF;
                std_btn[MUTE_BTN].show    = OFF;
                // Turn OFF Panel 3 buttons
                std_btn[MENU_BTN].show    = OFF;
                std_btn[ANT_BTN].show     = OFF;
                std_btn[ATU_BTN].show     = OFF;
                std_btn[XMIT_BTN].show    = OFF;
                std_btn[BANDDN_BTN].show  = OFF;
                std_btn[BANDUP_BTN].show  = OFF;
                // Turn OFF Panel 4 buttons
                std_btn[RIT_BTN].show     = OFF;
                std_btn[XIT_BTN].show     = OFF;
                std_btn[VFO_AB_BTN].show  = OFF;
                std_btn[FINE_BTN].show    = OFF;
                std_btn[DISPLAY_BTN].show = OFF;
                std_btn[SPLIT_BTN].show   = OFF;              
                //std_btn[ENET_BTN].show    = OFF;          
            }
            displayRefresh();   // redraw button to show new ones and hide old ones.  Set arg =1 to skip calling ourself
            Serial.print("Fn Pressed ");  Serial.println(std_btn[FN_BTN].enabled);
            return;
        }
    }

    // DISPLAY Test button (hidden area)
    if ((x>700 && x<800)&&(y>300 && y<400))
    {
        // DISPLAY BUTTON  - test usage today for spectum mostly
        //Draw a black box where the old window was
        
        uint8_t s = spectrum_preset;
        tft.fillRect(Sp_Parms_Def[s].spect_x, Sp_Parms_Def[s].spect_y, Sp_Parms_Def[s].spect_width, Sp_Parms_Def[s].spect_height, RA8875_BLACK);  // x start, y start, width, height, array of colors w x h
        spectrum_preset += 1;
        if (spectrum_preset > PRESETS-1)
            spectrum_preset = 0;         
        drawSpectrumFrame(spectrum_preset);
        spectrum_wf_style = Sp_Parms_Custom[spectrum_preset].spect_wf_style;
        displayRefresh();   // redraw the rest of the screen and buttons
        /*
        Sp_Parms_Def[spectrum_preset].spect_wf_colortemp += 10;
        if (Sp_Parms_Def[spectrum_preset].spect_wf_colortemp > 10000)
             Sp_Parms_Def[spectrum_preset].spect_wf_colortemp = 1;              
        Serial.print("spectrum_wf_colortemp = ");
        Serial.println(Sp_Parms_Def[spectrum_preset].spect_wf_colortemp);
        */
        /*
        spectrum_update_set += 1;
        if (spectrum_update_set > 6)
            spectrum_update_set = 0;         
            */
        Spectrum_Parm_Generator(spectrum_preset);  // Generate values for current display (on the fly) or filling in teh ddefauil table for Presets.  value of 0 to PRESETS.
        return;
    }   
}
