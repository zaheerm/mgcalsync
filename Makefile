all: gcalsync.cpp
	g++ -o gcalsync gcalsync.cpp `pkg-config --cflags --libs glib-2.0 libgdata calendar-backend sqlite3`

install:
	install -d ${DESTDIR}/opt/mgcalsync
	install -m 0755 gcalsync ${DESTDIR}/opt/mgcalsync
