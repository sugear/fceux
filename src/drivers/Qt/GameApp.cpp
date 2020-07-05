// GameApp.cpp
//
#include <QFileDialog>

#include "Qt/GameApp.h"
#include "Qt/GamePadConf.h"
#include "Qt/GameSoundConf.h"
#include "Qt/fceuWrapper.h"
#include "Qt/keyscan.h"
#include "Qt/nes_shm.h"

gameWin_t::gameWin_t(QWidget *parent)
	: QMainWindow( parent )
{

	createMainMenu();

	viewport = new gameViewGL_t(this);
	//viewport = new gameViewSDL_t(this);

   setCentralWidget(viewport);

	gameTimer  = new QTimer( this );
	gameThread = new QThread( this );
	worker     = new gameWorkerThread_t();
	mutex      = new QMutex( QMutex::NonRecursive );

	worker->moveToThread(gameThread);
   connect(gameThread, &QThread::finished, worker, &QObject::deleteLater);
	connect(gameThread, SIGNAL (started()), worker, SLOT( runEmulator() ));
	connect(worker, SIGNAL (finished()), gameThread, SLOT (quit()));
	connect(worker, SIGNAL (finished()), worker, SLOT (deleteLater()));

	connect( gameTimer, &QTimer::timeout, this, &gameWin_t::runGameFrame );

	gameTimer->setTimerType( Qt::PreciseTimer );
	gameTimer->start( 16 );

	gameThread->start();

   gamePadConfWin = NULL;
}

gameWin_t::~gameWin_t(void)
{
	nes_shm->runEmulator = 0;

   if ( gamePadConfWin != NULL )
   {
      gamePadConfWin->closeWindow();
   }
	fceuWrapperLock();
	fceuWrapperClose();
	fceuWrapperUnLock();

	//printf("Thread Finished: %i \n", gameThread->isFinished() );
	gameThread->exit(0);
	gameThread->wait();

	delete viewport;
	delete mutex;
}

void gameWin_t::setCyclePeriodms( int ms )
{
	// If timer is already running, it will be restarted.
	gameTimer->start( ms );
   
	//printf("Period Set to: %i ms \n", ms );
}

void gameWin_t::closeEvent(QCloseEvent *event)
{
   //printf("Main Window Close Event\n");
   if ( gamePadConfWin != NULL )
   {
      //printf("Command Game Pad Close\n");
      gamePadConfWin->closeWindow();
   }
   event->accept();

	closeApp();
}

void gameWin_t::keyPressEvent(QKeyEvent *event)
{
   //printf("Key Press: 0x%x \n", event->key() );
	pushKeyEvent( event, 1 );
}

void gameWin_t::keyReleaseEvent(QKeyEvent *event)
{
   //printf("Key Release: 0x%x \n", event->key() );
	pushKeyEvent( event, 0 );
}

//---------------------------------------------------------------------------
void gameWin_t::createMainMenu(void)
{
    // This is needed for menu bar to show up on MacOS
	 menuBar()->setNativeMenuBar(false);

	 //-----------------------------------------------------------------------
	 // File
    fileMenu = menuBar()->addMenu(tr("File"));

	 // File -> Open ROM
	 openROM = new QAction(tr("Open ROM"), this);
    openROM->setShortcuts(QKeySequence::Open);
    openROM->setStatusTip(tr("Open ROM File"));
    connect(openROM, SIGNAL(triggered()), this, SLOT(openROMFile(void)) );

    fileMenu->addAction(openROM);

	 // File -> Close ROM
	 closeROM = new QAction(tr("Close ROM"), this);
    closeROM->setShortcut( QKeySequence(tr("Ctrl+C")));
    closeROM->setStatusTip(tr("Close Loaded ROM"));
    connect(closeROM, SIGNAL(triggered()), this, SLOT(closeROMCB(void)) );

    fileMenu->addAction(closeROM);

    fileMenu->addSeparator();

	 // File -> Quit
	 quitAct = new QAction(tr("Quit"), this);
    quitAct->setShortcut( QKeySequence(tr("Ctrl+Q")));
    quitAct->setStatusTip(tr("Quit the Application"));
    connect(quitAct, SIGNAL(triggered()), this, SLOT(closeApp()));

    fileMenu->addAction(quitAct);

	 //-----------------------------------------------------------------------
	 // Options
    optMenu = menuBar()->addMenu(tr("Options"));

	 // Options -> GamePad Config
	 gamePadConfig = new QAction(tr("GamePad Config"), this);
    //gamePadConfig->setShortcut( QKeySequence(tr("Ctrl+C")));
    gamePadConfig->setStatusTip(tr("GamePad Configure"));
    connect(gamePadConfig, SIGNAL(triggered()), this, SLOT(openGamePadConfWin(void)) );

    optMenu->addAction(gamePadConfig);

	 // Options -> Sound Config
	 gameSoundConfig = new QAction(tr("Sound Config"), this);
    //gameSoundConfig->setShortcut( QKeySequence(tr("Ctrl+C")));
    gameSoundConfig->setStatusTip(tr("Sound Configure"));
    connect(gameSoundConfig, SIGNAL(triggered()), this, SLOT(openGameSndConfWin(void)) );

    optMenu->addAction(gameSoundConfig);

	 //-----------------------------------------------------------------------
	 // Help
    helpMenu = menuBar()->addMenu(tr("Help"));

	 aboutAct = new QAction(tr("About"), this);
    aboutAct->setStatusTip(tr("About Qplot"));
    connect(aboutAct, SIGNAL(triggered()), this, SLOT(aboutQPlot(void)) );

    helpMenu->addAction(aboutAct);
};
//---------------------------------------------------------------------------
void gameWin_t::closeApp(void)
{
	nes_shm->runEmulator = 0;

	fceuWrapperLock();
	fceuWrapperClose();
	fceuWrapperUnLock();

	// LoadGame() checks for an IP and if it finds one begins a network session
	// clear the NetworkIP field so this doesn't happen unintentionally
	g_config->setOption ("SDL.NetworkIP", "");
	g_config->save ();
	//SDL_Quit (); // Already called by fceuWrapperClose

	//qApp::quit();
	qApp->quit();
}
//---------------------------------------------------------------------------

void gameWin_t::openROMFile(void)
{
	int ret;
	QString filename;
	QFileDialog  dialog(this, "Open ROM File");

	dialog.setFileMode(QFileDialog::ExistingFile);

	dialog.setNameFilter(tr("All files (*.*) ;; NES files (*.nes)"));

	dialog.setViewMode(QFileDialog::List);

	// the gnome default file dialog is not playing nice with QT.
	// TODO make this a config option to use native file dialog.
	dialog.setOption(QFileDialog::DontUseNativeDialog, true);

	dialog.show();
	ret = dialog.exec();

	if ( ret )
	{
		QStringList fileList;
		fileList = dialog.selectedFiles();

		if ( fileList.size() > 0 )
		{
			filename = fileList[0];
		}
	}

   //filename =  QFileDialog::getOpenFileName( this,
   //       "Open ROM File",
   //       QDir::currentPath(),
   //       "All files (*.*) ;; NES files (*.nes)");
 
   if ( filename.isNull() )
   {
      return;
   }
	qDebug() << "selected file path : " << filename.toUtf8();

	g_config->setOption ("SDL.LastOpenFile", filename.toStdString().c_str() );

	fceuWrapperLock();
	CloseGame ();
	LoadGame ( filename.toStdString().c_str() );
	fceuWrapperUnLock();

   return;
}

void gameWin_t::closeROMCB(void)
{
	fceuWrapperLock();
	CloseGame();
	fceuWrapperUnLock();
}

void gameWin_t::openGamePadConfWin(void)
{
   if ( gamePadConfWin != NULL )
   {
      printf("GamePad Config Window Already Open\n");
      return;
   }
	//printf("Open GamePad Config Window\n");
   gamePadConfWin = new GamePadConfDialog_t(this);
	
   gamePadConfWin->show();
   gamePadConfWin->exec();

   delete gamePadConfWin;
   gamePadConfWin = NULL;
   //printf("GamePad Config Window Destroyed\n");
}

void gameWin_t::openGameSndConfWin(void)
{
	GameSndConfDialog_t *sndConfWin;

	printf("Open Sound Config Window\n");
	
   sndConfWin = new GameSndConfDialog_t(this);
	
   sndConfWin->show();
   sndConfWin->exec();

   delete sndConfWin;

   printf("Sound Config Window Destroyed\n");
}

void gameWin_t::aboutQPlot(void)
{
   printf("About QPlot\n");
   return;
}

void gameWin_t::runGameFrame(void)
{
	//struct timespec ts;
	//double t;

	//clock_gettime( CLOCK_REALTIME, &ts );

	//t = (double)ts.tv_sec + (double)(ts.tv_nsec * 1.0e-9);
   //printf("Run Frame %f\n", t);
	
	viewport->repaint();

   return;
}

void gameWorkerThread_t::runEmulator(void)
{
	printf("Emulator Start\n");
	nes_shm->runEmulator = 1;

	while ( nes_shm->runEmulator )
	{
		fceuWrapperUpdate();
	}
	printf("Emulator Exit\n");
	emit finished();
}
