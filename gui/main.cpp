#include "mainwindow.h"
#include <QApplication>
#include <QFile>
#include "screen.h"
#include "vomit.h"

static MainWindow *mw = 0L;

bool disklog, trapint, iopeek, mempeek;

int
main( int argc, char **argv )
{
	QApplication app( argc, argv );

	QStringList args = app.arguments();

	memset( &options, 0, sizeof(options) );
	if( args.contains( "--disklog" )) disklog = true;
	if( args.contains( "--trapint" )) trapint = true;
	if( args.contains( "--mempeek" )) mempeek = true;
	if( args.contains( "--iopeek" )) iopeek = true;
	if( args.contains( "--bda-peek" )) options.bda_peek = true;
	if( args.contains( "--trace" )) options.trace = true;

#ifndef VOMIT_TRACE
	if( options.trace )
	{
		fprintf( stderr, "Rebuild with #define VOMIT_TRACE if you want --trace to work.\n" );
		exit( 1 );
	}
#endif

	extern void vomit_disasm_init_tables();
	vomit_disasm_init_tables();

	QFile::remove( "log.txt" );

	cpu_genmap();

	int rc = vomit_init( argc, argv );

	if( rc != 0 )
	{
		fprintf( stderr, "vomit_init() returned %d\n", rc );
		return rc;
	}

	mw = new MainWindow;

	mw->show();

	return app.exec();
}

word
kbd_getc()
{
	if( !mw || !mw->screen() )
		return 0x0000;
	return mw->screen()->nextKey();
}

word
kbd_hit()
{
	if( !mw || !mw->screen() )
		return 0x0000;
	return mw->screen()->peekKey();
}

byte
kbd_pop_raw()
{
	if( !mw || !mw->screen() )
		return 0x00;
	return mw->screen()->popKeyData();
}

