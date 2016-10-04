/*  Ekos Internal Guider Class
    Copyright (C) 2016 Jasem Mutlaq <mutlaqja@ikarustech.com>.

    Based on lin_guider

    This application is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.
*/

#include <KMessageBox>
#include <KNotification>

#include "internalguider.h"
#include "gmath.h"

#include "ekos/auxiliary/QProgressIndicator.h"

#include "Options.h"


namespace Ekos
{

InternalGuider::InternalGuider()
{
    // Create math object
    pmath = new cgmath();

    connect(pmath, SIGNAL(newAxisDelta(double,double)), this, SIGNAL(newAxisDelta(double,double)));
    connect(pmath, SIGNAL(newAxisDelta(double,double)), this, SLOT(updateGuideDriver(double,double)));
    //connect(pmath, SIGNAL(newStarPosition(QVector3D,bool)), this, SLOT(setStarPosition(QVector3D,bool)));
    connect(pmath, SIGNAL(newStarPosition(QVector3D,bool)), this, SIGNAL(newStarPosition(QVector3D,bool)));

    // Calibration
    calibrationStage = CAL_IDLE;

    is_started = false;
    axis = GUIDE_RA;
    auto_drift_time = 5;

    start_x1 = start_y1 = 0;
    end_x1 = end_y1 = 0;
    start_x2 = start_y2 = 0;
    end_x2 = end_y2 = 0;

    idleColor.setRgb(200,200,200);
    okColor = Qt::green;
    busyColor = Qt::yellow;
    alertColor = Qt::red;
}

InternalGuider::~InternalGuider()
{
}

bool InternalGuider::guide()
{
return false;
}

bool InternalGuider::abort()
{
    calibrationStage = CAL_IDLE;
    return true;
}

bool InternalGuider::suspend()
{
return false;
}

bool InternalGuider::resume()
{
return false;
}

bool InternalGuider::dither(double pixels)
{
return false;
}

void InternalGuider::setSquareAlgorithm(int index)
{
    pmath->setSquareAlgorithm(index);
}

void InternalGuider::setReticleParameters(double x, double y, double angle)
{
    pmath->setReticleParameters(x,y,angle);
}

bool InternalGuider::getReticleParameters(double *x, double *y, double *angle)
{
    return pmath->getReticleParameters(x,y,angle);
}

bool InternalGuider::setGuiderParams(double ccdPixelSizeX, double ccdPixelSizeY, double mountAperture, double mountFocalLength)
{
    this->ccdPixelSizeX     = ccdPixelSizeX;
    this->ccdPixelSizeY     = ccdPixelSizeY;
    this->mountAperture     = mountAperture;
    this->mountFocalLength  = mountFocalLength;
    return pmath->setGuiderParameters(ccdPixelSizeX, ccdPixelSizeY, mountAperture, mountFocalLength);
}

bool InternalGuider::setFrameParams(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t binX, uint16_t binY)
{
    if( w <= 0 || h <= 0 )
        return false;

    subX = x;
    subY = y;
    subW = w;
    subH = h;

    subBinX = binX;
    subBinY = binY;

    pmath->setVideoParameters(w, h);

    return true;
}

bool InternalGuider::calibrate()
{
    bool ccdInfo=true, scopeInfo=true;
    QString errMsg;

    if (subW == 0 || subH == 0)
    {
        errMsg = "CCD";
        ccdInfo = false;
    }

    if (mountAperture == 0 || mountFocalLength == 0)
    {
        scopeInfo = false;
        if (ccdInfo == false)
            errMsg += " & Telescope";
        else
            errMsg += "Telescope";
    }

    if (ccdInfo == false || scopeInfo == false)
    {
        KMessageBox::error(NULL, i18n("Missing Information"), i18n("%1 info are missing. Please set the values in INDI Control Panel.", errMsg));
        return false;
    }

    if (state != GUIDE_CALIBRATING)
    {
        calibrationStage = CAL_IDLE;
        state = GUIDE_CALIBRATING;
        emit newStatus(GUIDE_CALIBRATING);
    }

    // Capture final image

    // FIXME check how to do manual
    // and fucking document it
    /*
    if (calibrationType == CAL_MANUAL && calibrationStage == CAL_START)
    {
        calibrationStage = CAL_CAPTURE_IMAGE;
        emit frameCaptureRequested();
        return;
    }*/



    //startCalibration();


    /*if (guideModule->isGuiding())
    {
        guideModule->appendLogText(i18n("Cannot calibrate while autoguiding is active."));
        return false;
    }*/

    if (calibrationStage > CAL_START)
    {
        //abort();
        processCalibration();
        return true;
    }

    disconnect(guideFrame, SIGNAL(trackingStarSelected(int,int)), this, SLOT(trackingStarSelected(int, int)));

    // Must reset dec swap before we run any calibration procedure!

    emit DESwapChanged(false);
    pmath->setDeclinationSwapEnabled(false);
    pmath->setLostStar(false);
    //pmain_wnd->capture();

    calibrationStage = CAL_START;


    // automatic
    // If two axies (RA/DEC) are required
    if( Options::twoAxisEnabled() )
        calibrateRADECRecticle(false);
    else
    // Just RA
        calibrateRADECRecticle(true);

    return true;
}

bool InternalGuider::stopCalibration()
{
    if (!pmath)
        return false;

    calibrationStage = CAL_ERROR;

    emit newStatus(Ekos::GUIDE_CALIBRATION_ERROR);

    reset();

    return true;
}

bool InternalGuider::startCalibration()
{
return true;
}

void InternalGuider::processCalibration()
{
    //if (pmath->get_image())
    //guide_frame->setTrackingBox(QRect(pmath-> square_pos.x, square_pos.y, square_size*2, square_size*2));
    //pmath->get_image()->setTrackingBoxSize(QSize(pmath->get_square_size(), pmath->get_square_size()));

    pmath->performProcessing();

    if (pmath->isStarLost())
    {        
        //ui.startCalibrationLED->setColor(alertColor);
        //KMessageBox::error(NULL, i18n("Lost track of the guide star. Try increasing the square size or reducing pulse duration."));
        emit newLog(i18n("Lost track of the guide star. Try increasing the square size or reducing pulse duration."));
        reset();

        calibrationStage = CAL_ERROR;
        emit newStatus(Ekos::GUIDE_CALIBRATION_ERROR);

        return;
    }

    switch (calibrationType)
    {
    case CAL_NONE:
        break;

    case CAL_RA_AUTO:
        calibrateRADECRecticle(true);
        break;

    case CAL_RA_DEC_AUTO:
        calibrateRADECRecticle(false);
        break;
    }
}

/*bool InternalGuider::isCalibrating()
{
    if (calibrationStage >= CAL_START)
        return true;

    return false;
}*/

void InternalGuider::setGuideView(FITSView *guideView)
{
    guideFrame = guideView;

    pmath->setGuideView(guideFrame);

    //connect(guideFrame, SIGNAL(trackingStarSelected(int,int)), this, SLOT(trackingStarSelected(int, int)), Qt::UniqueConnection);
}

void InternalGuider::reset()
{
    //FIXME

    is_started = false;
    state = GUIDE_IDLE;
    //calibrationStage = CAL_IDLE;
    connect(guideFrame, SIGNAL(trackingStarSelected(int,int)), this, SLOT(trackingStarSelected(int, int)), Qt::UniqueConnection);

#if 0
    is_started = false;
    ui.pushButton_StartCalibration->setText( i18n("Start") );
    ui.startCalibrationLED->setColor(idleColor);
    ui.progressBar->setVisible(false);
    connect(pmath->getImageView(), SIGNAL(trackingStarSelected(int,int)), this, SLOT(trackingStarSelected(int, int)), Qt::UniqueConnection);

#endif
}

void InternalGuider::calibrateRADECRecticle( bool ra_only )
{

    bool auto_term_ok = false;

    Q_ASSERT(pmath);

    int pulseDuration = Options::calibrationPulseDuration();
    int totalPulse    = pulseDuration * Options::autoModeIterations();

    if (ra_only)
        calibrationType = CAL_RA_AUTO;
    else
        calibrationType = CAL_RA_DEC_AUTO;

    switch(calibrationStage)
    {

    case CAL_START:
        //----- automatic mode -----
        auto_drift_time = Options::autoModeIterations();

        if (ra_only)
            turn_back_time = auto_drift_time*2 + auto_drift_time/2;
        else
            turn_back_time = auto_drift_time*6;
        iterations = 0;

        emit newLog(i18n("GUIDE_RA drifting forward..."));

        pmath->getReticleParameters(&start_x1, &start_y1, NULL);

        if (Options::guideLogging())
            qDebug() << "Guide: Start X1 " << start_x1 << " Start Y1 " << start_y1;

        emit newPulse( RA_INC_DIR, pulseDuration );

        if (Options::guideLogging())
            qDebug() << "Guide: Iteration " << iterations << " Direction: " << RA_INC_DIR << " Duration: " << pulseDuration << " ms.";

        iterations++;

        calibrationStage = CAL_RA_INC;

        break;

    case CAL_RA_INC:
        emit newPulse( RA_INC_DIR, pulseDuration );

        if (Options::guideLogging())
        {
            // Star position resulting from LAST guiding pulse to mount
            double cur_x, cur_y;
            pmath->getStarScreenPosition( &cur_x, &cur_y );
            qDebug() << "Guide: Iteration #" << iterations-1 << ": STAR " << cur_x << "," << cur_y;
            qDebug() << "Guide: Iteration " << iterations << " Direction: " << RA_INC_DIR << " Duration: " << pulseDuration << " ms.";
        }

        iterations++;

        if (iterations == auto_drift_time)
            calibrationStage = CAL_RA_DEC;

        break;

    case CAL_RA_DEC:
    {
        if (iterations == auto_drift_time)
        {
            pmath->getStarScreenPosition( &end_x1, &end_y1 );
            if (Options::guideLogging())
                qDebug() << "Guide: End X1 " << end_x1 << " End Y1 " << end_y1;

            phi = pmath->calculatePhi( start_x1, start_y1, end_x1, end_y1 );
            ROT_Z = RotateZ( -M_PI*phi/180.0 ); // derotates...

            emit newLog(i18n("GUIDE_RA drifting reverse..."));

        }

        //----- Z-check (new!) -----
        double cur_x, cur_y;
        pmath->getStarScreenPosition( &cur_x, &cur_y );

        Vector star_pos = Vector( cur_x, cur_y, 0 ) - Vector( start_x1, start_y1, 0 );
        star_pos.y = -star_pos.y;
        star_pos = star_pos * ROT_Z;

        if (Options::guideLogging())
            qDebug() << "Guide: Star x pos is " << star_pos.x << " from original point.";

        // start point reached... so exit
        if( star_pos.x < 1.5 )
        {
            pmath->performProcessing();
            auto_term_ok = true;
        }

        //----- Z-check end -----

        if( !auto_term_ok )
        {
            if (iterations < turn_back_time)
            {
                emit newPulse( RA_DEC_DIR, pulseDuration );

                if (Options::guideLogging())
                {
                    // Star position resulting from LAST guiding pulse to mount
                    double cur_x, cur_y;
                    pmath->getStarScreenPosition( &cur_x, &cur_y );
                    qDebug() << "Guide: Iteration #" << iterations-1 << ": STAR " << cur_x << "," << cur_y;
                    qDebug() << "Guide: Iteration " << iterations << " Direction: " << RA_INC_DIR << " Duration: " << pulseDuration << " ms.";
                }

                iterations++;
                break;
            }

            calibrationStage = CAL_ERROR;

            emit newStatus(Ekos::GUIDE_CALIBRATION_ERROR);

            emit newLog(i18np("GUIDE_RA: Scope cannot reach the start point after %1 iteration. Possible mount or drive problems...", "GUIDE_RA: Scope cannot reach the start point after %1 iterations. Possible mount or drive problems...", turn_back_time));

            KNotification::event( QLatin1String( "CalibrationFailed" ) , i18n("Guiding calibration failed with errors"));
            reset();
            break;
        }

        if (ra_only == false)
        {
            calibrationStage = CAL_DEC_INC;
            start_x2 = cur_x;
            start_y2 = cur_y;

            if (Options::guideLogging())
                qDebug() << "Guide: Start X2 " << start_x2 << " start Y2 " << start_y2;

            emit newPulse( DEC_INC_DIR, pulseDuration );

            if (Options::guideLogging())
            {
                // Star position resulting from LAST guiding pulse to mount
                double cur_x, cur_y;
                pmath->getStarScreenPosition( &cur_x, &cur_y );
                qDebug() << "Guide: Iteration #" << iterations-1 << ": STAR " << cur_x << "," << cur_y;
                qDebug() << "Guide: Iteration " << iterations << " Direction: " << RA_INC_DIR << " Duration: " << pulseDuration << " ms.";
            }

            iterations++;
            dec_iterations = 1;            
            emit newLog(i18n("GUIDE_DEC drifting forward..."));
            break;
        }
        // calc orientation
        if( pmath->calculateAndSetReticle1D( start_x1, start_y1, end_x1, end_y1, totalPulse) )
        {
            calibrationStage = CAL_IDLE;

            // FIXME what is this for?
            //fillInterface();

            emit newLog(i18n("Calibration completed."));

            emit newStatus(Ekos::GUIDE_CALIBRATION_SUCESS);

            KNotification::event( QLatin1String( "CalibrationSuccessful" ) , i18n("Guiding calibration completed successfully"));
            //if (ui.autoStarCheck->isChecked())
                //guideModule->selectAutoStar();
        }
        else
        {
            emit newLog(i18n("Calibration rejected. Star drift is too short."));

            calibrationStage = CAL_ERROR;

            emit newStatus(Ekos::GUIDE_CALIBRATION_ERROR);

            KNotification::event( QLatin1String( "CalibrationFailed" ) , i18n("Guiding calibration failed with errors"));
        }

        reset();
        break;
    }

    case CAL_DEC_INC:
        emit newPulse( DEC_INC_DIR, pulseDuration );

        if (Options::guideLogging())
        {
            // Star position resulting from LAST guiding pulse to mount
            double cur_x, cur_y;
            pmath->getStarScreenPosition( &cur_x, &cur_y );
            qDebug() << "Guide: Iteration #" << iterations-1 << ": STAR " << cur_x << "," << cur_y;
            qDebug() << "Guide: Iteration " << iterations << " Direction: " << RA_INC_DIR << " Duration: " << pulseDuration << " ms.";
        }

        iterations++;
        dec_iterations++;

        if (dec_iterations == auto_drift_time)
            calibrationStage = CAL_DEC_DEC;

        break;

    case CAL_DEC_DEC:
    {
        if (dec_iterations == auto_drift_time)
        {
            pmath->getStarScreenPosition( &end_x2, &end_y2 );
            if (Options::guideLogging())
                qDebug() << "Guide: End X2 " << end_x2 << " End Y2 " << end_y2;

            phi = pmath->calculatePhi( start_x2, start_y2, end_x2, end_y2 );
            ROT_Z = RotateZ( -M_PI*phi/180.0 ); // derotates...

            emit newLog(i18n("GUIDE_DEC drifting reverse..."));
        }

        //----- Z-check (new!) -----
        double cur_x, cur_y;
        pmath->getStarScreenPosition( &cur_x, &cur_y );

        //pmain_wnd->appendLogText(i18n("GUIDE_DEC running back...");

        if (Options::guideLogging())
            qDebug() << "Guide: Cur X2 " << cur_x << " Cur Y2 " << cur_y;

        Vector star_pos = Vector( cur_x, cur_y, 0 ) - Vector( start_x2, start_y2, 0 );
        star_pos.y = -star_pos.y;
        star_pos = star_pos * ROT_Z;

        if (Options::guideLogging())
            qDebug() << "Guide: start Pos X " << star_pos.x << " from original point.";

        // start point reached... so exit
        if( star_pos.x < 1.5 )
        {
            pmath->performProcessing();
            auto_term_ok = true;
        }

        //----- Z-check end -----

        if( !auto_term_ok )
        {
            if (iterations < turn_back_time)
            {
                emit newPulse(DEC_DEC_DIR, pulseDuration );

                if (Options::guideLogging())
                {
                    // Star position resulting from LAST guiding pulse to mount
                    double cur_x, cur_y;
                    pmath->getStarScreenPosition( &cur_x, &cur_y );
                    qDebug() << "Guide: Iteration #" << iterations-1 << ": STAR " << cur_x << "," << cur_y;
                    qDebug() << "Guide: Iteration " << iterations << " Direction: " << RA_INC_DIR << " Duration: " << pulseDuration << " ms.";
                }

                iterations++;
                dec_iterations++;
                break;
            }

            calibrationStage = CAL_ERROR;

            emit newStatus(Ekos::GUIDE_CALIBRATION_ERROR);

            emit newLog(i18np("GUIDE_DEC: Scope cannot reach the start point after %1 iteration.\nPossible mount or drive problems...", "GUIDE_DEC: Scope cannot reach the start point after %1 iterations.\nPossible mount or drive problems...", turn_back_time));

            KNotification::event( QLatin1String( "CalibrationFailed" ) , i18n("Guiding calibration failed with errors"));
            reset();
            break;
        }

        bool swap_dec=false;
        // calc orientation
        if( pmath->calculateAndSetReticle2D( start_x1, start_y1, end_x1, end_y1, start_x2, start_y2, end_x2, end_y2, &swap_dec, totalPulse ) )
        {
            calibrationStage = CAL_IDLE;
            //fillInterface();
            if (swap_dec)
               emit newLog(i18n("DEC swap enabled."));
            else
               emit newLog(i18n("DEC swap disabled."));

            emit newLog(i18n("Calibration completed."));

            emit newStatus(Ekos::GUIDE_CALIBRATION_SUCESS);

            emit DESwapChanged(swap_dec);

            KNotification::event( QLatin1String( "CalibrationSuccessful" ) , i18n("Guiding calibration completed successfully"));

            //if (ui.autoStarCheck->isChecked())
                //guideModule->selectAutoStar();

        }
        else
        {
            emit newLog(i18n("Calibration rejected. Star drift is too short."));

            emit newStatus(Ekos::GUIDE_CALIBRATION_ERROR);

            //ui.startCalibrationLED->setColor(alertColor);
            calibrationStage = CAL_ERROR;
            KNotification::event( QLatin1String( "CalibrationFailed" ) , i18n("Guiding calibration failed with errors"));
        }

        reset();

        break;
    }


    default:
        break;

    }
}

void InternalGuider::setStarPosition(QVector3D starCenter)
{
    pmath->setReticleParameters(starCenter.x(), starCenter.y(), -1);
}

void InternalGuider::trackingStarSelected(int x, int y)
{
    if (calibrationStage == CAL_IDLE)
        return;

    //int square_size = guide_squares[pmath->getSquareIndex()].size;

    pmath->setReticleParameters(x, y, -1);
    //pmath->moveSquare(x-square_size/(2*pmath->getBinX()), y-square_size/(2*pmath->getBinY()));

    //update_reticle_pos(x, y);



    //ui.selectStarLED->setColor(okColor);

    calibrationStage = CAL_START;

    //ui.pushButton_StartCalibration->setEnabled(true);

    QVector3D starCenter; // = guideModule->getStarPosition();
    starCenter.setX(x);
    starCenter.setY(y);    
    emit newStarPosition(starCenter, true);

    //if (ui.autoStarCheck->isChecked())
    if (Options::autoStarEnabled())
        calibrate();
}

#if 0
void InternalGuider::capture()
{
    /*
    if (isCalibrating())
        stopCalibration();

    calibrationStage = CAL_CAPTURE_IMAGE;

    if (guideModule->capture())
    {
        ui.captureLED->setColor(busyColor);
        guideModule->appendLogText(i18n("Capturing image..."));
    }
    else
    {
        ui.captureLED->setColor(alertColor);
        calibrationStage = CAL_ERROR;
        emit newStatus(Ekos::GUIDE_CALIBRATION_ERROR);
    }
    */
}
#endif

//FIXME
#if 0
bool InternalGuider::setImageView(FITSView *image)
{
    guideFrame = image;

    switch (calibrationStage)
    {
    case CAL_CAPTURE_IMAGE:
    case CAL_SELECT_STAR:
    {
        guideModule->appendLogText(i18n("Image captured..."));

        ui.captureLED->setColor(okColor);
        calibrationStage = CAL_SELECT_STAR;
        ui.selectStarLED->setColor(busyColor);

        FITSData *image_data = guideFrame->getImageData();

        setVideoParams(image_data->getWidth(), image_data->getHeight());

        if (ui.autoStarCheck->isChecked())
        {
            bool rc = guideModule->selectAutoStar();

            if (rc == false)
            {
                guideModule->appendLogText(i18n("Failed to automatically select a guide star. Please select a guide star..."));
                connect(guideFrame, SIGNAL(trackingStarSelected(int,int)), this, SLOT(trackingStarSelected(int, int)), Qt::UniqueConnection);
                return true;
            }
            else
                trackingStarSelected(guideModule->getStarPosition().x(), guideModule->getStarPosition().y());
            return false;
        }
        else
        {
            connect(guideFrame, SIGNAL(trackingStarSelected(int,int)), this, SLOT(trackingStarSelected(int, int)), Qt::UniqueConnection);
        }
    }
        break;

    default:
        break;
    }

    return true;
}
#endif

//FIXME

#if 0
void InternalGuider::setCalibrationTwoAxis(bool enable)
{
    ui.twoAxisCheck->setChecked(enable);
}

void InternalGuider::setCalibrationAutoStar(bool enable)
{
    ui.autoStarCheck->setChecked(enable);
}

void InternalGuider::setCalibrationAutoSquareSize(bool enable)
{
    ui.autoSquareSizeCheck->setChecked(enable);
}

void InternalGuider::setCalibrationPulseDuration(int pulseDuration)
{
    ui.spinBox_Pulse->setValue(pulseDuration);
}

void InternalGuider::toggleAutoSquareSize(bool enable)
{
    ui.autoSquareSizeCheck->setEnabled(enable);
}

#endif

void InternalGuider::setDECSwap(bool enable)
{
    pmath->setDeclinationSwapEnabled(enable);
}
}