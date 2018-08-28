.PHONY:all
all:insert select

select:select.c
	gcc $^ -o $@ -L /usr/lib64/mysql -lmysqlclient
insert:insert.c
	gcc $^ -o $@ -L /usr/lib64/mysql -lmysqlclient

.PHONY:clean
clean:
	rm select insert
