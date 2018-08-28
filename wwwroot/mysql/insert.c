//从数据库查找信息
//CGI程序

#include<stdio.h>//标准输入标准输出
#include<mysql/mysql.h>
#include "cgi_base.h"

int main(){
    // 0. 获取 query_string
    char buf[1024 * 4] = {0};
    if(GetQueryString(buf) < 0){
        fprintf(stderr, "GetQueryString failed\n");
        return 1;
    }
    // 约定客户端传递过来的参数格式：id=123&name=hehe
    int id =0;
    char name[1024] ={0};
    sscanf(buf, "id=%d&name=%s", &id, name);
    fprintf(stderr, "[CGI] id=%d name=%s\n", id, name);
    /////////////////////////////////////////////////
    // 接下来就要进行数据库的查找
    // 直接把一个数据库表中的所有数据都一股脑查出来
    // mysql api 使用的一般思路：
    // 1. 连接到数据库
    // 2. 拼接sql语句
    // 3. 把sql语句发送到服务器
    // 4. 读取并遍历服务器返回的结果
    // 5. 断开连接
    /////////////////////////////////////////////////

    // 1. 连接到数据库
    //这其实是一个句柄，什么是句柄？相当于一个遥控器，我们拿着遥控器就可以操作数据库
    //下面语句是为了得到一个空的句柄
    MYSQL* connect_fd = mysql_init(NULL);
    MYSQL* connect_ret = mysql_real_connect(connect_fd, "127.0.0.1", "root", 
            "zxy970922", "TestDB", 3306, NULL, 0);//真正的去连接数据库
    //第一个参数，句柄，得把遥控器放进来
    //第二个参数，ip地址和端口号 127.0.0.1 环回IP
    //第三个参数，用户名
    //第四个参数，密码
    //第五个参数，要访问的数据库
    //第六个参数，Mysql的端口号
    if(connect_ret == NULL){
        fprintf(stderr, "mysql connect failed!\n");
        return 1;
        //什么情况下会连接失败？
        //1. IP地址写错了
        //2. 用户名密码不正确
        //3. 数据库名字不正确
        //4. 用户无权限访问数据库也会连接失败
        //   ...
    }
    fprintf(stderr, "mysql connect ok!\n");

    // 2. 拼接sql语句
    //    组织命令
    char sql[1024 * 4] = {0};
    sprintf(sql, "insert into TestTable values(%d, '%s')", id, name);//期望用户通过浏览器插入数据

    // 3. 把sql语句发送到服务器
    //    使用遥控器把命令发给服务器
    int ret = mysql_query(connect_fd, sql);
    if(ret < 0){
        //说明出错，什么情况下会出错？
        //例如：语法错误
        fprintf(stderr, "mysql_query failed! %s\n", sql);
        return 1;
    }

    // 4. 向客户端反馈插入成功失败的结果 通过ret查看
    if(ret == 0){
        printf("<html><h1>插入成功</h1><html>");
    }else{
        printf("<html><h1>插入失败</h1><html>");
    }

    // 5. 断开连接
    mysql_close(connect_fd);
    return 0;
}
