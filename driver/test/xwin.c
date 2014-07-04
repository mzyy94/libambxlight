#include <X11/Xlib.h>
#include <stdio.h>

#define WIN_W  400  /* ウィンドウの幅   */
#define WIN_H  300  /* ウィンドウの高さ */
#define WIN_X  100  /* ウィンドウ表示位置(X) */
#define WIN_Y  100  /* ウィンドウ表示位置(Y) */
#define BORDER 2    /* ボーダの幅      */


int 
main( void )
{
	Display*       dpy;          /* ディスプレイ ( X サーバのこと) */
	Window         root;         /* ルートウィンドウ ( デスクトップ ) */
	Window         win;          /* 表示するウィンドウ */
	int            screen;       /* スクリーン */
	unsigned long  black, white; /* 黒と白のピクセル値 */
	XEvent         evt;          /* イベント構造体変数 */


	/* Xサーバと接続する */
	dpy = XOpenDisplay( ":1" );


	/* ディスプレイ変数の取得 */
	root   = DefaultRootWindow( dpy );
	screen = DefaultScreen( dpy );


	/* 白、黒のピクセル値を取得 */
	white  = WhitePixel( dpy, screen );
	black  = BlackPixel( dpy, screen );
	printf("white: %u\n", white);
	printf("black: %u\n", black);


	/* ウィンドウを作成する。この時点ではまだ画面に表示されない。*/
	win = XCreateSimpleWindow( dpy, root,
		   WIN_X, WIN_Y, WIN_W, WIN_H, BORDER, black, white);


	/* Xサーバから通知してもらうイベントを指定
	   ここではキー押下イベントを選択している */
	XSelectInput( dpy, win, KeyPressMask );


	/* ウィンドウを画面に表示する(マップする)。 */
	XMapWindow( dpy, win );


	/* 僕の環境では XCreateSimpleWindow() での座標指定だけでは希望の座標
	   にウィンドウが表示されないので、明示的に希望の座標にウィンドウを
	   移動させるために XMoveWindow() を使いました。もし (WIN_X, WIN_Y)に
	   ちゃんと表示されない場合はやってみてください。 */

	/* XMoveWindow( dpy, win, WIN_X, WIN_Y ); */


	/* Xサーバへのリクエストバッファをフラッシュする。 */
	/* XFlush( dpy ); */


	/* Xサーバからイベントを読みこんで随時処理していくイベントループ */
	while( 1 ) {
		XNextEvent( dpy, &evt );

		switch ( evt.type ) {
			case KeyPress:
				/* リソースの解放 */
				XDestroyWindow( dpy, win );
				XCloseDisplay( dpy );
				return 0;
		}
	}
}

/* End of xwin.c */
