
all:
	gcc -Wall -o mvproc_fcgi mvp_fcgi.c mvp_config.c mvp_parser.c mvp_mysql.c mvp_output.c mvp_filler.c -lfcgi -lmysqlclient_r -lcrypto

