#include "comparison.h"
#include "ui_comparison.h"
#include <QBuffer>
#include <QProcess>
#include <QMessageBox>
#include <QTextEdit>
#include <QWheelEvent>

Comparison::Comparison(QVector<Video *> &userVideos, Prefs &userPrefs, QWidget &parent)
    : QDialog(&parent), ui(new Ui::Comparison)
{
    _videos = userVideos;
    _prefs = userPrefs;
    _mainWindow = &parent;

    ui->setupUi(this);
    setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint);
    connect(this, SIGNAL(sendStatusMessage(QString)), _mainWindow, SLOT(addStatusMessage(QString)));

    if(_prefs._ComparisonMode == _prefs._SSIM)
        ui->selectSSIM->setChecked(true);
    _prefs._thresholdPhashOriginal = _prefs._thresholdPhash;
    ui->thresholdSlider->setValue(64 - _prefs._thresholdPhash);

    updateProgressbar(true);
    on_nextVideo_clicked();
}

Comparison::~Comparison()
{
    if(_videosDeleted > 0)
        emit sendStatusMessage(QString("\n%1 file(s) deleted, %2 freed").
                               arg(_videosDeleted).arg(readableFileSize(_spaceSaved)));
    delete ui;
}

void Comparison::on_prevVideo_clicked()
{
    _seekForwards = false;
    const int numberOfVideos = _videos.count();
    int right = _rightVideo - 1;                //click prev button: start at current videos,
    for(int left=_leftVideo; left>=0; left--)   //go backwards and compare each combination
    {                                           //until a match is found
        for(; right>left; right--)
        {
            if(bothVideosMatch(left, right))
            {
                ui->progressBar->setValue(ui->progressBar->value() - 1);
                if(QFileInfo::exists(_videos[left]->filename) &&
                   QFileInfo::exists(_videos[right]->filename))
                {
                    _leftVideo = left;
                    _rightVideo = right;
                    showVideo("left");
                    showVideo("right");
                    highlightBetterProperties();
                    updateUI();
                    return;
                }
            }
        }
        right = numberOfVideos - 1;
    }
}

void Comparison::on_nextVideo_clicked()
{
    _seekForwards = true;
    const int oldLeft = _leftVideo;
    const int oldRight = _rightVideo;

    const int numberOfVideos = _videos.count();
    for(; _leftVideo<=numberOfVideos; _leftVideo++)
    {
        for(_rightVideo++; _rightVideo<numberOfVideos; _rightVideo++)
        {
            if(bothVideosMatch(_leftVideo, _rightVideo))
            {
                ui->progressBar->setValue(ui->progressBar->value() + 1);
                if(QFileInfo::exists(_videos[_leftVideo]->filename) &&
                   QFileInfo::exists(_videos[_rightVideo]->filename))
                {
                    showVideo("left");
                    showVideo("right");
                    highlightBetterProperties();
                    updateUI();
                    return;
                }
            }
        }
        _rightVideo = _leftVideo + 1;
    }

    _leftVideo = oldLeft;       //went over limit, go back
    _rightVideo = oldRight;

    QMessageBox::StandardButton confirm = QMessageBox::Yes;
    if(ui->leftFileName->text() != "")  //no results, close window automatically
    {
        const QString askEnd = QString("Close window?\n(comparison results will be discarded)");
        confirm = QMessageBox::question(this, "Out of videos to compare", askEnd, QMessageBox::Yes|QMessageBox::No);
    }
    if(confirm == QMessageBox::Yes)
    {
        QKeyEvent *closeEvent = new QKeyEvent(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::postEvent(this, dynamic_cast<QEvent *>(closeEvent));  //"pressing" ESC closes dialog
    }
}

bool Comparison::bothVideosMatch(const int &left, const int &right) const
{
    const short similarity = phashSimilarity(left, right);
    if(_prefs._ComparisonMode == _prefs._PHASH)
    {
        if(similarity <= _prefs._thresholdPhash)
            return true;
        return false;
    }
    else if(similarity <= qMax(_prefs._thresholdPhash, static_cast<short>(20)))
    {   //ssim comparison takes long time, only do it if pHash somewhat matches
        double threshold = _prefs._thresholdSSIM;
        const double ss = ssim(_videos[left]->grayThumb, _videos[right]->grayThumb, _prefs._ssimBlockSize);

        if(_videos[right]->duration > _videos[left]->duration * 0.96 &&
           _videos[right]->duration < _videos[left]->duration * 1.04)
            threshold = threshold - _prefs._sameDurationModifier / 64;        //lower distance if video lengths are within 4%
        else
            threshold = threshold + _prefs._differentDurationModifier / 64;   //raise distance if video lengths differ over 4%

        if(ss > threshold)
            return true;
    }
    return false;
}

short Comparison::phashSimilarity(const int &left, const int &right) const
{
    short distance = 0;

    uint64_t differentBits = _videos[left]->hash ^ _videos[right]->hash;     //XOR to value (only ones for differing bits)
    while(differentBits != 0)
    {
        differentBits &= differentBits - 1;     //count number of bits of value
        distance++;
    }
    if(_videos[right]->duration > _videos[left]->duration * 0.96 &&
       _videos[right]->duration < _videos[left]->duration * 1.04)
        distance = distance - _prefs._sameDurationModifier;        //lower distance if video lengths are within 4%
    else
        distance = distance + _prefs._differentDurationModifier;   //raise distance if video lengths differ over 4%

    return distance > 0? distance : 0;  //negative value would wrap into huge value because return value is short
                                        //could return ushort, but "-2/64 different bits" looks weird (although useful)
}

void Comparison::showVideo(const QString &side) const
{
    int thisVideo = _leftVideo;
    if(side == "right")
        thisVideo = _rightVideo;

    auto *Image = this->findChild<ClickableLabel *>(side + "Image");
    QByteArray uncompressed = qUncompress(_videos[thisVideo]->thumbnail);
    QBuffer pixels(&uncompressed);
    QImage image;
    image.load(&pixels, "JPG");
    Image->setPixmap(QPixmap::fromImage(image).scaled(Image->width(), Image->height(), Qt::KeepAspectRatio));

    auto *FileName = this->findChild<ClickableLabel *>(side + "FileName");
    FileName->setText(QFileInfo(_videos[thisVideo]->filename).fileName());
    FileName->setToolTip(QString("%1\nOpen in file manager").arg(QDir::toNativeSeparators(_videos[thisVideo]->filename)));

    QFileInfo videoFile(_videos[thisVideo]->filename);
    auto *PathName = this->findChild<QLabel *>(side + "PathName");
    PathName->setText(QDir::toNativeSeparators(videoFile.absolutePath()));

    auto *FileSize = this->findChild<QLabel *>(side + "FileSize");
    FileSize->setText(readableFileSize(_videos[thisVideo]->size));

    auto *Duration = this->findChild<QLabel *>(side + "Duration");
    Duration->setText(readableDuration(_videos[thisVideo]->duration));

    auto *Modified = this->findChild<QLabel *>(side + "Modified");
    Modified->setText(_videos[thisVideo]->modified.toString("yyyy-MM-dd hh:mm:ss"));

    const QString resolutionString = QString("%1x%2").
                  arg(_videos[thisVideo]->width).arg(_videos[thisVideo]->height);
    auto *Resolution = this->findChild<QLabel *>(side + "Resolution");
    Resolution->setText(resolutionString);

    auto *FrameRate = this->findChild<QLabel *>(side + "FrameRate");
    const double fps = _videos[thisVideo]->framerate;
    if(fps == 0.0)
        FrameRate->setText("");
    else
        FrameRate->setText(QString("%1 FPS").arg(fps));

    auto *BitRate = this->findChild<QLabel *>(side + "BitRate");
    BitRate->setText(readableBitRate(_videos[thisVideo]->bitrate));

    auto *Codec = this->findChild<QLabel *>(side + "Codec");
    Codec->setText(_videos[thisVideo]->codec);

    auto *Audio = this->findChild<QLabel *>(side + "Audio");
    Audio->setText(_videos[thisVideo]->audio);
}

QString Comparison::readableDuration(const qint64 &milliseconds) const
{
    if(milliseconds == 0)
        return "";

    const ushort hours   = ((milliseconds / (1000*60*60)) % 24);
    const ushort minutes = ((milliseconds / (1000*60)) % 60);
    const ushort seconds = (milliseconds / 1000) % 60;

    QString readableDuration;
    if(hours > 0)
        readableDuration = QString("%1h").arg(hours);
    if(minutes > 0)
        readableDuration = QString("%1%2m").arg(readableDuration).arg(minutes);
    if(seconds > 0)
        readableDuration = QString("%1%2s").arg(readableDuration).arg(seconds);

    return readableDuration;
}

QString Comparison::readableFileSize(const qint64 &filesize) const
{
    if(filesize < 1024 * 1024)
        return QString::number(static_cast<double>(filesize) / 1024, 'i', 0) + " kB";    //small files = even kBs
    else if(filesize < 1024 * 1024 * 1024)                          //larger files have one decimal point
        return QString::number(static_cast<double>(filesize) / (1024 * 1024), 'f', 1) + " MB";
    else
        return QString::number(static_cast<double>(filesize) / (1024 * 1024 * 1024), 'f', 1) + " GB";
}

QString Comparison::readableBitRate(const double &kbps) const
{
    if(kbps == 0.0)
        return "";
    return QString::number(kbps) + " kb/s";
}

void Comparison::highlightBetterProperties() const
{
    ui->leftFileSize->setStyleSheet("");
    ui->rightFileSize->setStyleSheet("");               //both filesizes within 100 kb
    if(qAbs(_videos[_leftVideo]->size - _videos[_rightVideo]->size) <= 1024*100)
    {
        ui->leftFileSize->setStyleSheet("QLabel { color : tan; }");
        ui->rightFileSize->setStyleSheet("QLabel { color : tan; }");
    }
    else if(_videos[_leftVideo]->size > _videos[_rightVideo]->size)
        ui->leftFileSize->setStyleSheet("QLabel { color : green; }");
    else if(_videos[_leftVideo]->size < _videos[_rightVideo]->size)
        ui->rightFileSize->setStyleSheet("QLabel { color : green; }");

    ui->leftDuration->setStyleSheet("");
    ui->rightDuration->setStyleSheet("");               //both runtimes within 1 second
    if(qAbs(_videos[_leftVideo]->duration - _videos[_rightVideo]->duration) <= 1000)
    {
        ui->leftDuration->setStyleSheet("QLabel { color : tan; }");
        ui->rightDuration->setStyleSheet("QLabel { color : tan; }");
    }
    else if(_videos[_leftVideo]->duration > _videos[_rightVideo]->duration)
        ui->leftDuration->setStyleSheet("QLabel { color : green; }");
    else if(_videos[_leftVideo]->duration < _videos[_rightVideo]->duration)
        ui->rightDuration->setStyleSheet("QLabel { color : green; }");

    ui->leftBitRate->setStyleSheet("");
    ui->rightBitRate->setStyleSheet("");
    if(_videos[_leftVideo]->bitrate == _videos[_rightVideo]->bitrate)
    {
        ui->leftBitRate->setStyleSheet("QLabel { color : tan; }");
        ui->rightBitRate->setStyleSheet("QLabel { color : tan; }");
    }
    else if(_videos[_leftVideo]->bitrate > _videos[_rightVideo]->bitrate)
        ui->leftBitRate->setStyleSheet("QLabel { color : green; }");
    else if(_videos[_leftVideo]->bitrate < _videos[_rightVideo]->bitrate)
        ui->rightBitRate->setStyleSheet("QLabel { color : green; }");

    ui->leftFrameRate->setStyleSheet("");
    ui->rightFrameRate->setStyleSheet("");              //both framerates within 0.1 fps
    if(qAbs(_videos[_leftVideo]->framerate - _videos[_rightVideo]->framerate) <= 0.1)
    {
        ui->leftFrameRate->setStyleSheet("QLabel { color : tan; }");
        ui->rightFrameRate->setStyleSheet("QLabel { color : tan; }");
    }
    else if(_videos[_leftVideo]->framerate > _videos[_rightVideo]->framerate)
        ui->leftFrameRate->setStyleSheet("QLabel { color : green; }");
    else if(_videos[_leftVideo]->framerate < _videos[_rightVideo]->framerate)
        ui->rightFrameRate->setStyleSheet("QLabel { color : green; }");

    ui->leftModified->setStyleSheet("");
    ui->rightModified->setStyleSheet("");
    if(_videos[_leftVideo]->modified == _videos[_rightVideo]->modified)
    {
        ui->leftModified->setStyleSheet("QLabel { color : tan; }");
        ui->rightModified->setStyleSheet("QLabel { color : tan; }");
    }
    else if(_videos[_leftVideo]->modified < _videos[_rightVideo]->modified)
        ui->leftModified->setStyleSheet("QLabel { color : green; }");
    else if(_videos[_leftVideo]->modified > _videos[_rightVideo]->modified)
        ui->rightModified->setStyleSheet("QLabel { color : green; }");

    ui->leftResolution->setStyleSheet("");
    ui->rightResolution->setStyleSheet("");

    if(_videos[_leftVideo]->width * _videos[_leftVideo]->height ==
       _videos[_rightVideo]->width * _videos[_rightVideo]->height)
    {
        ui->leftResolution->setStyleSheet("QLabel { color : tan; }");
        ui->rightResolution->setStyleSheet("QLabel { color : tan; }");
    }
    else if(_videos[_leftVideo]->width * _videos[_leftVideo]->height >
       _videos[_rightVideo]->width * _videos[_rightVideo]->height)
        ui->leftResolution->setStyleSheet("QLabel { color : green; }");
    else if(_videos[_leftVideo]->width * _videos[_leftVideo]->height <
            _videos[_rightVideo]->width * _videos[_rightVideo]->height)
        ui->rightResolution->setStyleSheet("QLabel { color : green; }");
}

void Comparison::updateUI()
{
    if(ui->leftPathName->text() == ui->rightPathName->text())    //gray out move button if both videos in same folder
    {
        ui->leftMove->setDisabled(true);
        ui->rightMove->setDisabled(true);
    }
    else
    {
        ui->leftMove->setDisabled(false);
        ui->rightMove->setDisabled(false);
    }

    if(_prefs._ComparisonMode == _prefs._PHASH)
        ui->identicalBits->setText(QString("%1/64 different bits").arg(phashSimilarity(_leftVideo, _rightVideo)));
    if(_prefs._ComparisonMode == _prefs._SSIM)
    {
        const double ss = ssim(_videos[_leftVideo]->grayThumb, _videos[_rightVideo]->grayThumb, _prefs._ssimBlockSize);
        ui->identicalBits->setText(QString("%1 SSIM index").arg(QString::number(ss, 'f', 3)));
    }

    _zoomLevel = 0;
}

void Comparison::updateProgressbar(bool alsoReport) const
{
    const int numberOfVideos = _videos.count();
    ui->progressBar->setMaximum(-1);
    if( (_prefs._ComparisonMode == _prefs._PHASH && numberOfVideos > 15000) ||
        (_prefs._ComparisonMode == _prefs._SSIM && numberOfVideos > 7500) )
    {
        emit sendStatusMessage("Progress bar disabled to avoid slowdown (too many videos to calculate)");
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);

    int permutations = 0;
    int currentPosition = 0;
    QVector<int> knownMatches;
    qint64 combinedFilesize = 0;

    if(alsoReport)
    {
        const QString str = QString("\n[%1] Counting match combinations (may take long time with many videos)")
                            .arg(QTime::currentTime().toString());
        emit sendStatusMessage(str);
    }

    for(int left=0; left<numberOfVideos; left++)
    {
        int videosMatchingLeft = 0;
        for(int right=left+1; right<numberOfVideos; right++)
            if(bothVideosMatch(left, right))
            {
                permutations++;
                if(left == _leftVideo && right == _rightVideo)
                    currentPosition = permutations;

                videosMatchingLeft++;
                if(alsoReport && videosMatchingLeft <= 1)
                {
                    bool alreadyFound = false;
                    for(int i=0; i<knownMatches.count(); i++)
                        if(knownMatches[i] == right)   //matching videos only counted ONCE
                            alreadyFound = true;
                    if(!alreadyFound)
                    {
                        knownMatches.append(right);
                        //smaller of two matching videos is likely the one to be deleted
                        combinedFilesize += std::min(_videos[left]->size , _videos[right]->size);
                    }
                }
            }
    }

    if(permutations == 0)   //annoying flashing progress bar when slider dragged to minimum
        ui->progressBar->setMaximum(-1);
    else
        ui->progressBar->setMaximum(permutations);
    ui->progressBar->setValue(currentPosition);

    if(alsoReport)
    {
        const QString results = QString("[%1] Found %2 video(s) (%3) with matches").arg(
                      QTime::currentTime().toString()).arg(knownMatches.count()).arg(readableFileSize(combinedFilesize));
        emit sendStatusMessage(results);
    }
    QApplication::restoreOverrideCursor();
}

//clicking on filename opens folder with file selected
void Comparison::on_leftFileName_clicked() const
{
    QString exploreVideo = "";
    #if defined(Q_OS_WIN)
        exploreVideo = QString("explorer /select, \"%1\"").
                       arg(QDir::toNativeSeparators(_videos[_leftVideo]->filename));
    #endif
    #if defined(Q_OS_MACX)
        exploreVideo = QString("open -R \"%1\"").arg(_videos[_leftVideo]->filename);
    #endif
    #if defined(Q_OS_X11)
        const QFileInfo videoFile(_videos[_leftVideo]->filename);
        const QString pathString = QString("%1").arg(videoFile.absolutePath());
        exploreVideo = QString("xdg-open \"%1\"").arg(pathString);
    #endif

    QProcess::startDetached(exploreVideo);
}
void Comparison::on_rightFileName_clicked() const
{
    QString exploreVideo = "";
    #if defined(Q_OS_WIN)
        exploreVideo = QString("explorer /select, \"%1\"").
                       arg(QDir::toNativeSeparators(_videos[_rightVideo]->filename));
    #endif
    #if defined(Q_OS_MACX)
        exploreVideo = QString("open -R \"%1\"").arg(_videos[_rightVideo]->filename);
    #endif
    #if defined(Q_OS_X11)
        const QFileInfo videoFile(_videos[_rightVideo]->filename);
        const QString pathString = QString("%1").arg(videoFile.absolutePath());
        exploreVideo = QString("xdg-open \"%1\"").arg(pathString);
    #endif

    QProcess::startDetached(exploreVideo);
}

void Comparison::on_leftDelete_clicked()
{
    //left side video was already manually deleted, skip to next
    if(!QFileInfo::exists(_videos[_leftVideo]->filename))
    {
        _leftVideo++;
        _rightVideo = _leftVideo;
        _seekForwards? on_nextVideo_clicked() : on_prevVideo_clicked();
        return;
    }

    const QString askDelete = QString("Are you sure you want to delete this file?\n\n%1").
            arg(ui->leftFileName->text());
    QMessageBox::StandardButton confirm;
    confirm = QMessageBox::question(this, "Delete file", askDelete, QMessageBox::Yes|QMessageBox::No);

    if(confirm == QMessageBox::Yes)
    {
        QFile file(_videos[_leftVideo]->filename);
        if(!file.remove())
        {
            QMessageBox msgBox;
            msgBox.setText("Could not delete file. Check file permissions.");
            msgBox.exec();
        }
        else
        {
            _videosDeleted++;
            _spaceSaved = _spaceSaved + _videos[_leftVideo]->size;
            emit sendStatusMessage(QString("Deleted %1").arg(QDir::toNativeSeparators(_videos[_leftVideo]->filename)));

            _leftVideo++;
            _rightVideo = _leftVideo;
            _seekForwards? on_nextVideo_clicked() : on_prevVideo_clicked();
            updateProgressbar();
        }
    }
}

void Comparison::on_rightDelete_clicked()
{
    //right side video was already manually deleted, skip to next
    if(!QFileInfo::exists(_videos[_rightVideo]->filename))
    {
        _seekForwards? on_nextVideo_clicked() : on_prevVideo_clicked();
        return;
    }

    const QString askDelete = QString("Are you sure you want to delete this file?\n\n%1").
            arg(ui->rightFileName->text());
    QMessageBox::StandardButton confirm;
    confirm = QMessageBox::question(this, "Delete file", askDelete, QMessageBox::Yes|QMessageBox::No);
    if(confirm == QMessageBox::Yes)
    {
        QFile file(_videos[_rightVideo]->filename);
        if(!file.remove())
        {
            QMessageBox msgBox;
            msgBox.setText("Could not delete file. Check file permissions.");
            msgBox.exec();
        }
        else
        {
            _videosDeleted++;
            _spaceSaved = _spaceSaved + _videos[_rightVideo]->size;
            emit sendStatusMessage(QString("Deleted %1").arg(QDir::toNativeSeparators(_videos[_rightVideo]->filename)));
            _seekForwards? on_nextVideo_clicked() : on_prevVideo_clicked();
            updateProgressbar();
        }
    }
}

void Comparison::on_leftMove_clicked()
{
    //left side video was already manually deleted, skip to next
    if(!QFileInfo::exists(_videos[_leftVideo]->filename))
    {
        _leftVideo++;
        _rightVideo = _leftVideo;
        _seekForwards? on_nextVideo_clicked() : on_prevVideo_clicked();
        return;
    }

    const QString askMove = QString("Are you sure you want to move this file?\n\nFrom: %1\nTo:     %2")
            .arg(ui->leftPathName->text(), ui->rightPathName->text());
    QMessageBox::StandardButton confirm;
    confirm = QMessageBox::question(this, "Move file", askMove, QMessageBox::Yes|QMessageBox::No);
    if(confirm == QMessageBox::Yes)
    {
        const QString renameTo = QString("%1/%2").arg(ui->rightPathName->text(), ui->leftFileName->text());
        QFile file(_videos[_leftVideo]->filename);
        if(!file.rename(renameTo))
        {
            QMessageBox msgBox;
            msgBox.setText("Could not move file. Check file permissions and available disk space.");
            msgBox.exec();
        }
        else
        {
            emit sendStatusMessage(QString("Moved %1 to %2").arg(QDir::toNativeSeparators(_videos[_leftVideo]->filename),
                                                                 ui->rightPathName->text()));
            _leftVideo++;
            _rightVideo = _leftVideo;
            _seekForwards? on_nextVideo_clicked() : on_prevVideo_clicked();
            updateProgressbar();
        }
    }
}

void Comparison::on_rightMove_clicked()
{
    //right side video was already manually deleted, skip to next
    if(!QFileInfo::exists(_videos[_rightVideo]->filename))
    {
        _seekForwards? on_nextVideo_clicked() : on_prevVideo_clicked();
        return;
    }

    const QString askMove = QString("Are you sure you want to move this file?\n\nFrom: %1\nTo:     %2")
            .arg(ui->rightPathName->text(), ui->leftPathName->text());
    QMessageBox::StandardButton confirm;
    confirm = QMessageBox::question(this, "Move file", askMove, QMessageBox::Yes|QMessageBox::No);
    if(confirm == QMessageBox::Yes)
    {
        const QString renameTo = QString("%1/%2").arg(ui->leftPathName->text(), ui->rightFileName->text());
        QFile file(_videos[_rightVideo]->filename);
        if(!file.rename(renameTo))
        {
            QMessageBox msgBox;
            msgBox.setText("Could not move file. Check file permissions and available disk space.");
            msgBox.exec();
        }
        else
        {
            emit sendStatusMessage(QString("Moved %1 to %2").arg(QDir::toNativeSeparators(_videos[_rightVideo]->filename),
                                                                 ui->leftPathName->text()));
            _seekForwards? on_nextVideo_clicked() : on_prevVideo_clicked();
            updateProgressbar();
        }
    }
}

void Comparison::on_swapFilenames_clicked()
{
    const QFileInfo leftVideoFile(_videos[_leftVideo]->filename);
    const QString leftPathname = leftVideoFile.absolutePath();
    const QString oldLeftFilename = leftVideoFile.fileName();
    const QString oldLeftNoExtension = oldLeftFilename.left(oldLeftFilename.lastIndexOf("."));
    const QString leftExtension = oldLeftFilename.right(oldLeftFilename.length() - oldLeftFilename.lastIndexOf("."));

    const QFileInfo rightVideoFile(_videos[_rightVideo]->filename);
    const QString rightPathname = rightVideoFile.absolutePath();
    const QString oldRightFilename = rightVideoFile.fileName();
    const QString oldRightNoExtension = oldRightFilename.left(oldRightFilename.lastIndexOf("."));
    const QString rightExtension = oldRightFilename.right(oldRightFilename.length() - oldRightFilename.lastIndexOf("."));

    const QString newLeftFilename = QString("%1%2").arg(oldRightNoExtension, leftExtension);
    const QString newLeftPathAndFilename = QString("%1/%2").arg(leftPathname, newLeftFilename);

    const QString newRightFilename = QString("%1%2").arg(oldLeftNoExtension, rightExtension);
    const QString newRightPathAndFilename = QString("%1/%2").arg(rightPathname, newRightFilename);

    QFile leftFile(_videos[_leftVideo]->filename);                  //rename files
    QFile rightFile(_videos[_rightVideo]->filename);
    leftFile.rename(QString("%1/VidupeRenamedVideo.avi").arg(leftPathname));
    rightFile.rename(newRightPathAndFilename);
    leftFile.rename(newLeftPathAndFilename);

    _videos[_leftVideo]->filename = newLeftPathAndFilename;         //update filename in object
    _videos[_rightVideo]->filename = newRightPathAndFilename;

    ui->leftFileName->setText(newLeftFilename);                     //update UI
    ui->rightFileName->setText(newRightFilename);
}

void Comparison::on_thresholdSlider_valueChanged(const int &value)
{
    const int differentBits = 64 - value;
    const int percentage = 100 * value / 64;

    const QString thresholdMessage = QString("pHash threshold: %1% (%2/64 bits may differ),"
                                             " was originally %3%\nSSIM threshold: %4").
            arg(percentage).arg(differentBits).arg(100 * (64 - _prefs._thresholdPhashOriginal) / 64).
            arg(static_cast<double>(value) / 64);
    ui->thresholdSlider->setToolTip(thresholdMessage);

    if(differentBits != _prefs._thresholdPhash)    //function also called when constructor sets slider
    {
        _prefs._thresholdPhash = static_cast<short>(64 - value);
        _prefs._thresholdSSIM = static_cast<double>(value) / 64;
        updateProgressbar();
    }
}

void Comparison::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event);

    if(ui->leftFileName->text() == "" || _leftVideo >= _videos.count() || _rightVideo >= _videos.count())
        return;     //automatic initial resize event can happen before closing when values went over limit

    QByteArray uncompressed;
    QImage image;

    uncompressed = qUncompress(_videos[_leftVideo]->thumbnail);
    QBuffer leftPixels(&uncompressed);
    image.load(&leftPixels, "JPG");
    ui->leftImage->setPixmap(QPixmap::fromImage(image).scaled(
                             ui->leftImage->width(), ui->leftImage->height(), Qt::KeepAspectRatio));

    uncompressed = qUncompress(_videos[_rightVideo]->thumbnail);
    QBuffer rightPixels(&uncompressed);
    image.load(&rightPixels, "JPG");
    ui->rightImage->setPixmap(QPixmap::fromImage(image).scaled(
                              ui->rightImage->width(), ui->rightImage->height(), Qt::KeepAspectRatio));
}

void Comparison::wheelEvent(QWheelEvent *event)
{
    const QPoint pos = QCursor::pos();
    if(QApplication::widgetAt(pos)->objectName() != "leftImage" && QApplication::widgetAt(pos)->objectName() != "rightImage")
        return;

    if(_zoomLevel == 0)     //first mouse wheel movement: retrieve actual screen captures in full resolution
    {
        const QTemporaryDir tempDir1, tempDir2;
        if(!tempDir1.isValid() || !tempDir2.isValid())
            return;

        QImage image;
        image = _videos[_leftVideo]->captureAt(_videos[_leftVideo]->filename, tempDir1, 10);
        ui->leftImage->setPixmap(QPixmap::fromImage(image).scaled(
                                 ui->leftImage->width(), ui->leftImage->height(), Qt::KeepAspectRatio));
        _leftZoomed = QPixmap::fromImage(image);      //keep it in memory
        _leftW = image.width();
        _leftH = image.height();

        image = _videos[_rightVideo]->captureAt(_videos[_rightVideo]->filename, tempDir2, 10);
        ui->rightImage->setPixmap(QPixmap::fromImage(image).scaled(
                                  ui->rightImage->width(), ui->rightImage->height(), Qt::KeepAspectRatio));
        _rightZoomed = QPixmap::fromImage(image);
        _rightW = image.width();
        _rightH = image.height();

        _zoomLevel = 1;
        return;
    }

    if(event->delta() > 0 && _zoomLevel < 10)   //mouse wheel up
        _zoomLevel = _zoomLevel * 2;
    if(event->delta() < 0 && _zoomLevel > 1)    //mouse wheel down
        _zoomLevel = _zoomLevel / 2;

    QPixmap pix;
    pix = _leftZoomed.copy(_leftW/_zoomLevel, _leftH/_zoomLevel, _leftW/_zoomLevel, _leftH/_zoomLevel);
    ui->leftImage->setPixmap(pix.scaled(ui->leftImage->width(), ui->leftImage->height(),
                                        Qt::IgnoreAspectRatio, Qt::FastTransformation));

    pix = _rightZoomed.copy(_rightW/_zoomLevel, _rightH/_zoomLevel, _rightW/_zoomLevel, _rightH/_zoomLevel);
    ui->rightImage->setPixmap(pix.scaled(ui->rightImage->width(), ui->rightImage->height(),
                                         Qt::IgnoreAspectRatio, Qt::FastTransformation));
}
