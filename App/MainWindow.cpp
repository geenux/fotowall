/***************************************************************************
 *                                                                         *
 *   This file is part of the Fotowall project,                            *
 *       http://www.enricoros.com/opensource/fotowall                      *
 *                                                                         *
 *   Copyright (C) 2007-2009 by Enrico Ros <enrico.ros@gmail.com>          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "App/MainWindow.h"

#include "3rdparty/likebackfrontend/LikeBack.h"
#include "Canvas/CanvasModeInfo.h"
#include "Canvas/Canvas.h"
#include "Shared/ButtonsDialog.h"
#include "Shared/MetaXmlReader.h"
#include "Shared/RenderOpts.h"
#include "Shared/VideoProvider.h"
#include "WordCloud/WordCloud.h"
#include "App.h"
#include "CanvasAppliance.h"
#include "ExportWizard.h"
#include "SceneView.h"
#include "Settings.h"
#include "VersionCheckDialog.h"
#include "XmlRead.h"
#include "XmlSave.h"
#include "ui_MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDesktopWidget>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFile>
#include <QImageReader>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QProgressDialog>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include "math.h"

// current location and 'check string' for the tutorial
#define TUTORIAL_URL QUrl("http://fosswire.com/post/2008/09/fotowall-make-wallpaper-collages-from-your-photos/")
#define TUTORIAL_STRING "Peter walks you through how to use Foto"
#define ENRICOBLOG_STRING "http://www.enricoros.com/blog/tag/fotowall/"
#define FOTOWALL_FEEDBACK_LANGS "en,it,fr"
#define FOTOWALL_FEEDBACK_SERVER "www.enricoros.com"
#define FOTOWALL_FEEDBACK_PATH "/opensource/fotowall/feedback/send.php"


MainWindow::MainWindow(const QStringList & contentUrls, QWidget * parent)
    : Appliance::Container(parent)
    , ui(new Ui::MainWindow())
    , m_appManager(new Appliance::Manager)
    , m_aHelpTutorial(0)
    , m_likeBack(0)
{
    // setup widget
#if QT_VERSION >= 0x040500
    setWindowTitle(QCoreApplication::applicationName() + " " + QCoreApplication::applicationVersion());
#else
    setWindowTitle(QCoreApplication::applicationName() + " " + QCoreApplication::applicationVersion() + "   -Limited Edition (Qt 4.4)-");
#endif
    setWindowIcon(QIcon(":/data/fotowall.png"));

    // init ui
    ui->setupUi(this);
    ui->sceneView->setFocus();
#if QT_VERSION >= 0x040500
    ui->transpBox->setEnabled(true);
    ui->accelBox->setEnabled(ui->sceneView->supportsOpenGL());
#endif
    m_appManager->setContainer(this);
    connect(m_appManager, SIGNAL(structureChanged()), this, SLOT(slotApplianceStructureChanged()));

    // attach menus
    ui->onlineHelpButton->setMenu(createOnlineHelpMenu());

    // create a catch-all select action
    QAction * aSA = new QAction(tr("Select all"), this);
    aSA->setShortcut(tr("CTRL+A"));
    connect(aSA, SIGNAL(triggered()), this, SLOT(slotActionSelectAll()));
    addAction(aSA);

    // create likeback
    m_likeBack = new LikeBack(LikeBack::AllButtons, false, this);
    m_likeBack->setAcceptedLanguages(QString(FOTOWALL_FEEDBACK_LANGS).split(","));
    m_likeBack->setServer(FOTOWALL_FEEDBACK_SERVER, FOTOWALL_FEEDBACK_PATH);

    // show initially
    if (!restoreGeometry(App::settings->value("Fotowall/Geometry").toByteArray())) {
        QRect desktopGeometry = QApplication::desktop()->availableGeometry();
        resize(2 * desktopGeometry.width() / 3, 2 * desktopGeometry.height() / 3);
        showMaximized();
    } else
        show();

    // create a Canvas (and load/populate it)
    Canvas * canvas = new Canvas(this);
    // open if single fotowall file
    if (contentUrls.size() == 1 && App::isFotowallFile(contentUrls.first()))
        XmlRead::read(contentUrls.first(), canvas);

    // add if many pictures
    else if (!contentUrls.isEmpty())
        canvas->addPictureContent(contentUrls);

    // no url: display history
#if 0
    else {
        foreach (const QUrl & url, App::settings->recentFotowallUrls())
            canvas->addCanvasViewContent(QStringList() << url.toString());
    }
#endif

    // use the editing appliance over it
    editCanvas(canvas);

    // check stuff on the net
    checkForTutorial();
    checkForUpdates();
}

MainWindow::~MainWindow()
{
    // save window geometry
    if (!isMaximized() && !isFullScreen())
        App::settings->setValue("Fotowall/Geometry", saveGeometry());
    else
        App::settings->remove("Fotowall/Geometry");

    // this is an example of 'autosave-like function'
    //QString tempPath = QDir::tempPath() + QDir::separator() + "autosave.fotowall";
    //XmlSave::save(tempPath, m_canvas, m_canvas->modeInfo());

    // delete everything
    delete m_appManager;
    delete m_likeBack;
    delete ui;
}

void MainWindow::editCanvas(Canvas * canvas)
{
    CanvasAppliance * cApp = new CanvasAppliance(canvas, ui->sceneView, this);
    m_appManager->stackAppliance(cApp);
}

void MainWindow::editWordcloud(WordCloud::Cloud * cloud)
{
    HERE;
    //WordcloudAppliance * wApp = new WordcloudAppliance(cloud);
    //m_appManager->stackAppliance(wApp);
}

void MainWindow::showIntroduction()
{
    if (CanvasAppliance * cApp = dynamic_cast<CanvasAppliance *>(m_appManager->currentAppliance()))
        cApp->canvas()->showIntroduction();
}

// OK
void MainWindow::applianceSetScene(QGraphicsScene * scene)
{
    ui->sceneView->setScene(dynamic_cast<AbstractScene *>(scene));
}

// OK
void MainWindow::applianceSetTopbar(const QList<QWidget *> & widgets)
{
    // clear the topbar layout hiding all widgets
    while (QLayoutItem * oldItem = ui->applianceTopbarLayout->takeAt(0))
        if (QWidget * oldWidget = oldItem->widget())
            oldWidget->setVisible(false);

    // add the widgets to the topbar and show them
    foreach (QWidget * widget, widgets) {
        ui->applianceTopbarLayout->addWidget(widget);
        widget->setVisible(true);
    }
}

// OK
void MainWindow::applianceSetSidebar(QWidget * widget)
{
    ui->applianceSidebar->setVisible(widget);
    if (widget)
        ui->applianceSidebarLayout->addWidget(widget);
}

// OK
void MainWindow::applianceSetCentralwidget(QWidget * widget)
{
    if (widget)
        qWarning("MainWindow::applianceSetCentralwidget: unsupported");
}

void MainWindow::closeEvent(QCloseEvent * event)
{
    // build the closure dialog
#if 0
    ButtonsDialog quitAsk("MainWindow-Exit", tr("Closing Fotowall..."));
    quitAsk.setMinimumWidth(350);
    quitAsk.setButtonText(QDialogButtonBox::Cancel, tr("Cancel"));
    if (m_canvas && m_canvas->pendingChanges()) {
        quitAsk.setMessage(tr("Are you sure you want to quit and lose your changes?"));
        quitAsk.setButtonText(QDialogButtonBox::Save, tr("Save"));
        quitAsk.setButtonText(QDialogButtonBox::Close, tr("Don't Save"));
        quitAsk.setButtons(QDialogButtonBox::Save | QDialogButtonBox::Close | QDialogButtonBox::Cancel);
    } else {
        quitAsk.setMessage(tr("Are you sure you want to quit?"));
        quitAsk.setButtonText(QDialogButtonBox::Close, tr("Quit"));
        quitAsk.setButtons(QDialogButtonBox::Close | QDialogButtonBox::Cancel);
    }

    // react to the dialog's answer
    switch (quitAsk.execute()) {
        case QDialogButtonBox::Cancel:
            event->ignore();
            break;

        case QDialogButtonBox::Save:
            // save file and return to Fotowall if canceled
            if (!on_saveButton_clicked()) {
                event->ignore();
                break;
            }
            // fall through

        default:
            event->accept();
            break;
    }
#endif
}

QMenu * MainWindow::createOnlineHelpMenu()
{
    QMenu * menu = new QMenu();
    menu->setSeparatorsCollapsible(false);

    m_aHelpTutorial = new QAction(tr("Tutorial Video (0.2)"), menu);
    connect(m_aHelpTutorial, SIGNAL(triggered()), this, SLOT(slotHelpTutorial()));
    menu->addAction(m_aHelpTutorial);

    QAction * aCheckUpdates = new QAction(tr("Check for Updates"), menu);
    connect(aCheckUpdates, SIGNAL(triggered()), this, SLOT(slotHelpUpdates()));
    menu->addAction(aCheckUpdates);

    QAction * aFotowallBlog = new QAction(tr("Fotowall's Blog"), menu);
    connect(aFotowallBlog, SIGNAL(triggered()), this, SLOT(slotHelpWebsite()));
    menu->addAction(aFotowallBlog);

    return menu;
}

void MainWindow::checkForTutorial()
{
    // hide the tutorial link
    m_aHelpTutorial->setVisible(false);

    // try to get the tutorial page (note, multiple QNAMs will be deleted on app closure)
    QNetworkAccessManager * manager = new QNetworkAccessManager(this);
    connect(manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(slotVerifyTutorial(QNetworkReply*)));
    manager->get(QNetworkRequest(TUTORIAL_URL));
}

void MainWindow::checkForUpdates()
{
    // find out the time of the last update check
    QDate lastCheck = App::settings->value("Fotowall/LastUpdateCheck").toDate();
    if (lastCheck.isNull()) {
        App::settings->setValue("Fotowall/LastUpdateCheck", QDate::currentDate());
        return;
    }

    // check for updates 30 days after the last one
    if (lastCheck.daysTo(QDate::currentDate()) > 30)
        QTimer::singleShot(2000, this, SLOT(slotHelpUpdates()));
}

// OK
void MainWindow::slotApplianceStructureChanged()
{
    // build the new breadcrumbbar's contents
    ui->applianceNavBar->clearNodes();
    QList<Appliance::AbstractAppliance *> appliances = m_appManager->stackedAppliances();
    if (!appliances.isEmpty()) {
        quint32 index = 0;
        foreach (Appliance::AbstractAppliance * app, appliances) {
            ui->applianceNavBar->addNode(index + 1, app->applianceName(), index);
            index++;
        }
    }
    update();
}

void MainWindow::slotActionSelectAll()
{
    HERE;
    //m_canvas->selectAllContent();
}

void MainWindow::on_accelBox_toggled(bool enabled)
{
    // ask for confirmation when enabling opengl
    if (enabled) {
        ButtonsDialog warning("GoOpenGL", tr("OpenGL"), tr("OpenGL accelerates graphics. However it's not guaranteed that it will work on your system.<br>Just try and see if it works for you ;-)<br> - if it feels slower, make sure that your driver accelerates OpenGL<br> - if Fotowall stops responding after switching to OpenGL, just don't use this feature next time<br><br>NOTE: OpenGL doesn't work with 'Transparent' mode.<br>"), QDialogButtonBox::Ok | QDialogButtonBox::Cancel, true, true);
        warning.setIcon(QStyle::SP_MessageBoxInformation);
        if (warning.execute() == QDialogButtonBox::Cancel) {
            ui->accelBox->setChecked(false);
            return;
        }

        // toggle transparency with opengl
        ui->transpBox->setChecked(false);
    }

    // set opengl state
    ui->sceneView->setOpenGL(enabled);
}

#ifdef Q_WS_WIN
/**
  Blur behind windows (on Windows Vista/7)

  The following code snippet has been borrowed from Jens of Qt Software / Nokia
  see: http://labs.qt.nokia.com/blogs/2009/09/15/using-blur-behind-on-windows/
  the license says: Use, modification and distribution is allowed without
  limitation, warranty, liability or support of any kind.
**/
#include <QLibrary>
#include <qt_windows.h>

// Dwm Data Structures
#define DWM_BB_ENABLE                 0x00000001  // fEnable has been specified
typedef struct _DWM_BLURBEHIND
{
    DWORD dwFlags;
    BOOL fEnable;
    HRGN hRgnBlur;
    BOOL fTransitionOnMaximized;
} DWM_BLURBEHIND;

// Dwm entry points
typedef HRESULT (WINAPI *PtrDwmIsCompositionEnabled)(BOOL * pfEnabled);
typedef HRESULT (WINAPI *PtrDwmEnableBlurBehindWindow)(HWND hWnd, const DWM_BLURBEHIND * pBlurBehind);
static PtrDwmIsCompositionEnabled pDwmIsCompositionEnabled = 0;
static PtrDwmEnableBlurBehindWindow pDwmEnableBlurBehindWindow  = 0;

static bool dwmResolveLibs()
{
    if (!pDwmIsCompositionEnabled) {
        QLibrary dwmLib(QString::fromAscii("dwmapi"));
        pDwmIsCompositionEnabled = (PtrDwmIsCompositionEnabled)dwmLib.resolve("DwmIsCompositionEnabled");
        pDwmEnableBlurBehindWindow = (PtrDwmEnableBlurBehindWindow)dwmLib.resolve("DwmEnableBlurBehindWindow");
    }
    return pDwmIsCompositionEnabled != 0;
}

static bool dwmEnableBlurBehindWindow(QWidget * widget, bool enable)
{
    bool result = false;
    if (dwmResolveLibs()) {
        DWM_BLURBEHIND bb = {0};
        bb.dwFlags = DWM_BB_ENABLE;
        bb.fEnable = enable;
        bb.hRgnBlur = NULL;
        HRESULT hr = pDwmEnableBlurBehindWindow(widget->winId(), &bb);
        if (SUCCEEDED(hr))
            result = true;
    }
    return result;
}
#endif

void MainWindow::on_transpBox_toggled(bool transparent)
{
#if QT_VERSION >= 0x040500
    static Qt::WindowFlags initialWindowFlags = windowFlags();
    if (transparent) {
        // one-time warning
        ButtonsDialog warning("GoTransparent", tr("Transparency"), tr("This feature has not been widely tested yet.<br> - on linux it requires compositing (like compiz/beryl, kwin4)<br> - on windows and mac it seems to work<br>If you see a black background then transparency is not supported on your system.<br><br>NOTE: you should set the 'Transparent' Background to notice the the window transparency.<br>"), QDialogButtonBox::Ok, true, true);
        warning.setIcon(QStyle::SP_MessageBoxInformation);
        warning.execute();

        // go transparent
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);

        // hint the render that we're transparent now
        RenderOpts::ARGBWindow = true;

#ifdef Q_OS_WIN
        // enable blur behind on Vista/7
        if (!dwmEnableBlurBehindWindow(this, true)) {
            // if blur fails, use a frameless window that's needed on XP for transparency
            setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
            show();
        }
#endif
    } else {
        // back to normal (non-alphaed) window
        setAttribute(Qt::WA_TranslucentBackground, false);
        setAttribute(Qt::WA_NoSystemBackground, false);

#ifdef Q_OS_WIN
        // disable no-border on windows
        setWindowFlags(initialWindowFlags);
        show();
#endif

        // hint the render that we're opaque again
        RenderOpts::ARGBWindow = false;
    }
    // refresh the window
    update();
#else
    Q_UNUSED(transparent)
#endif
}

void MainWindow::on_introButton_clicked()
{
    showIntroduction();
}

void MainWindow::on_lbLike_clicked()
{
    m_likeBack->execCommentDialog(LikeBack::Like);
}

void MainWindow::on_lbDislike_clicked()
{
    m_likeBack->execCommentDialog(LikeBack::Dislike);
}

void MainWindow::on_lbFeature_clicked()
{
    m_likeBack->execCommentDialog(LikeBack::Feature);
}

void MainWindow::on_lbBug_clicked()
{
    m_likeBack->execCommentDialog(LikeBack::Bug);
}

bool MainWindow::on_loadButton_clicked()
{
    // make up the default load path (stored as 'Fotowall/LoadProjectDir')
    QString defaultLoadPath = App::settings->value("Fotowall/LoadProjectDir").toString();

    // ask the file name, validate it, store back to settings and load the file
    QString fileName = QFileDialog::getOpenFileName(this, tr("Select the Fotowall file"), defaultLoadPath, tr("Fotowall (*.fotowall)"));
    if (fileName.isNull())
        return false;
    App::settings->setValue("Fotowall/LoadProjectDir", QFileInfo(fileName).absolutePath());

    // try to load the canvas
    Canvas * canvas = new Canvas(this);
    if (!XmlRead::read(fileName, canvas)) {
        delete canvas;
        return false;
    }

    // close all and edit the loaded file
    m_appManager->clearAppliances();
    editCanvas(canvas);
    return true;
}

bool MainWindow::on_saveButton_clicked()
{
    // support saving only .fotowall files
    CanvasAppliance * cApp = dynamic_cast<CanvasAppliance *>(m_appManager->currentAppliance());
    if (!cApp)
        return false;

    // make up the default save path (stored as 'Fotowall/SaveProjectDir')
    QString defaultSavePath = tr("Unnamed %1.fotowall").arg(QDate::currentDate().toString());
    if (App::settings->contains("Fotowall/SaveProjectDir"))
        defaultSavePath.prepend(App::settings->value("Fotowall/SaveProjectDir").toString() + QDir::separator());

    // ask the file name, validate it, store back to settings and save over it
    QString fileName = QFileDialog::getSaveFileName(this, tr("Select the Fotowall file"), defaultSavePath, "Fotowall (*.fotowall)");
    if (fileName.isNull())
        return false;
    App::settings->setValue("Fotowall/SaveProjectDir", QFileInfo(fileName).absolutePath());
    if (!fileName.endsWith(".fotowall", Qt::CaseInsensitive))
        fileName += ".fotowall";
    return XmlSave::save(fileName, cApp->canvas());
}

void MainWindow::on_exportButton_clicked()
{
    CanvasAppliance * cApp = dynamic_cast<CanvasAppliance *>(m_appManager->currentAppliance());
    if (!cApp)
        return;

    // show the Export Wizard on normal mode
    Canvas * canvas = cApp->canvas();
    if (canvas->modeInfo()->projectMode() == CanvasModeInfo::ModeNormal) {
        ExportWizard(canvas).exec();
        return;
    }

    // print on other modes
    canvas->printAsImage(canvas->modeInfo()->printDpi(),
                         canvas->modeInfo()->fixedPrinterPixels(),
                         canvas->modeInfo()->printLandscape());
}

void MainWindow::slotHelpWebsite()
{
    // start a fetch if no URL has been determined
    if (m_website.isEmpty()) {
        MetaXml::Connector * conn = new MetaXml::Connector();
        connect(conn, SIGNAL(fetched()), this, SLOT(slotHelpWebsiteFetched()));
        connect(conn, SIGNAL(fetchError(const QString &)), this, SLOT(slotHelpWebsiteFetchError()));
        return;
    }

    // open the website
    int answer = QMessageBox::question(this, tr("Opening Fotowall's author Blog"), tr("This is the blog of the main author of Fotowall.\nYou can find some news while we set up a proper website ;-)\nDo you want to open the web page?"), QMessageBox::Yes, QMessageBox::No);
    if (answer == QMessageBox::Yes)
        QDesktopServices::openUrl(QUrl(m_website));
}

void MainWindow::slotHelpWebsiteFetched()
{
    // get the websites from the conn
    MetaXml::Connector * conn = dynamic_cast<MetaXml::Connector *>(sender());
    if (conn && !conn->reader()->websites.isEmpty()) {
        m_website = conn->reader()->websites.first().url;
        if (!m_website.isEmpty()) {
            slotHelpWebsite();
            return;
        }
    }

    // catch-all condition: use default url
    slotHelpWebsiteFetchError();
}

void MainWindow::slotHelpWebsiteFetchError()
{
    m_website = ENRICOBLOG_STRING;
    slotHelpWebsite();
}

void MainWindow::slotHelpTutorial()
{
    int answer = QMessageBox::question(this, tr("Opening the Web Tutorial"), tr("The Tutorial is provided on Fosswire by Peter Upfold.\nIt's about Fotowall 0.2 a rather old version.\nDo you want to open the web page?"), QMessageBox::Yes, QMessageBox::No);
    if (answer == QMessageBox::Yes)
        QDesktopServices::openUrl(TUTORIAL_URL);
}

void MainWindow::slotHelpUpdates()
{
    VersionCheckDialog vcd;
    vcd.exec();
    App::settings->setValue("Fotowall/LastUpdateCheck", QDate::currentDate());
}

void MainWindow::slotVerifyTutorial(QNetworkReply * reply)
{
    if (reply->error() != QNetworkReply::NoError)
        return;

    QString htmlCode = reply->readAll();
    bool tutorialValid = htmlCode.contains(TUTORIAL_STRING, Qt::CaseInsensitive);
    m_aHelpTutorial->setVisible(tutorialValid);
}