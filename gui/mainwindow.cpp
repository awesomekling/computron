#include "mainwindow.h"
#include "worker.h"
#include "console.h"
#include "screen.h"
#include <QToolBar>
#include <QAction>
#include <QFileDialog>
#include <QVBoxLayout>
#include <QTimer>
#include <QDebug>

extern "C" {
void vomit_set_drive_image( int drive_id, const char *filename );
};

struct MainWindow::Private
{
	QToolBar *mainToolBar;

	QAction *startMachine;
	QAction *pauseMachine;
	QAction *stopMachine;

	Worker worker;
	Screen screen;
	Console console;

	QTimer syncTimer;
};

MainWindow::MainWindow()
	: d( new Private )
{
	setWindowTitle( "Vomit" );

	QWidget *widget = new QWidget( this );
	widget->setWindowTitle( "VOMIT" );

	QVBoxLayout *l = new QVBoxLayout;
	l->setSpacing( 0 );
	l->setMargin( 0 );
	widget->setLayout( l );
	l->addWidget( &d->screen );
	//l->addWidget( activityBar );
	l->addWidget( &d->console );


	d->screen.setFocus();

	setCentralWidget( widget );

	QAction *chooseFloppyAImage = new QAction( QIcon("icons/media-floppy.png"), tr("Choose floppy A image..."), this );
	QAction *chooseFloppyBImage = new QAction( QIcon("icons/media-floppy.png"), tr("Choose floppy B image..."), this );
	d->pauseMachine = new QAction( QIcon("icons/media-playback-pause.png"), tr("Pause VM"), this );
	d->startMachine = new QAction( QIcon("icons/media-playback-start.png"), tr("Start VM"), this );
	d->stopMachine = new QAction( QIcon("icons/media-playback-stop.png"), tr("Start VM"), this );

	d->startMachine->setEnabled( false );
	d->pauseMachine->setEnabled( true );
	d->stopMachine->setEnabled( true );

	d->mainToolBar = addToolBar( tr("Virtual Machine") );

	d->mainToolBar->addAction( d->startMachine );
	d->mainToolBar->addAction( d->pauseMachine );
	d->mainToolBar->addAction( d->stopMachine );

	d->mainToolBar->addAction( chooseFloppyAImage );
	d->mainToolBar->addAction( chooseFloppyBImage );

	connect( chooseFloppyAImage, SIGNAL(triggered(bool)), SLOT(slotFloppyAClicked()) );
	connect( chooseFloppyBImage, SIGNAL(triggered(bool)), SLOT(slotFloppyBClicked()) );

	connect( d->pauseMachine, SIGNAL(triggered(bool)), SLOT(slotPauseMachine()) );
	connect( d->startMachine, SIGNAL(triggered(bool)), SLOT(slotStartMachine()) );
	connect( d->stopMachine, SIGNAL(triggered(bool)), SLOT(slotStopMachine()) );

	QObject::connect( &d->worker, SIGNAL( finished() ), this, SLOT( close() ));
	d->worker.startMachine();
	d->worker.start();

	QObject::connect( &d->syncTimer, SIGNAL( timeout() ), &d->screen, SLOT( refresh() ));
	QObject::connect( &d->syncTimer, SIGNAL( timeout() ), &d->screen, SLOT( flushKeyBuffer() ));
	d->syncTimer.start( 50 );
}

MainWindow::~MainWindow()
{
	delete d;
	d = 0L;
}

void
MainWindow::slotFloppyAClicked()
{
	QString fileName = QFileDialog::getOpenFileName(
		this,
		tr("Choose floppy B image")
	);
	if( fileName.isNull() )
		return;
	vomit_set_drive_image( 0, qPrintable(fileName) );
}

void
MainWindow::slotFloppyBClicked()
{
	QString fileName = QFileDialog::getOpenFileName(
		this,
		tr("Choose floppy B image")
	);
	if( fileName.isNull() )
		return;
	vomit_set_drive_image( 1, qPrintable(fileName) );
}

void
MainWindow::slotPauseMachine()
{
	d->pauseMachine->setEnabled( false );
	d->startMachine->setEnabled( true );
	d->stopMachine->setEnabled( true );

	d->screen.setTinted( true );

	d->worker.stopMachine();
}

void
MainWindow::slotStopMachine()
{
	d->pauseMachine->setEnabled( false );
	d->startMachine->setEnabled( true );
	d->stopMachine->setEnabled( false );

	d->screen.setTinted( true );

	d->worker.stopMachine();
}

void
MainWindow::slotStartMachine()
{
	d->pauseMachine->setEnabled( true );
	d->startMachine->setEnabled( false );
	d->stopMachine->setEnabled( true );

	d->screen.setTinted( false );

	d->worker.startMachine();
}

Screen *
MainWindow::screen()
{
	return &d->screen;
}