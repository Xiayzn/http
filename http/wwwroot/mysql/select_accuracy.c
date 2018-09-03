//从数据库查找信息
//CGI程序

#include<stdio.h>//标准输入标准输出
#include<mysql/mysql.h>
#include "cgi_base.h"

#define BURSIZE 2048 

int hex2dec(char c){       
    if ('0' <= c && c <= '9'){              
      return c - '0';                      
    }                           
    else if ('a' <= c && c <= 'f'){                              
      return c - 'a' + 10;             
    }                  
    else if ('A' <= c && c <= 'F'){                  
      return c - 'A' + 10;         
    }                                                                             
    else{
      return -1;
    }
  }

void urldecode(char url[]){
    int i = 0;
    int len = strlen(url);                         
    int res_len = 0;                                                          
    char res[BURSIZE];
    for (i = 0; i < len; ++i){  
        char c = url[i];
        if (c != '%'){ 
            res[res_len++] = c;
        }else{
            char c1 = url[++i];
            char c0 = url[++i];
            int num = 0;
            num = hex2dec(c1) * 16 + hex2dec(c0);
           res[res_len++] = num;
       }
    }
    res[res_len] = '\0';   
    strcpy(url, res);     
}        

int Split(char input[], char* split_char, 
    char* output[], int output_size){
    //使用 strtok — 字符串切割函数 实现的 Split ，但使用的时候需要循环调用.                                          
    int i = 0;                                                                                                        
    //char* tmp = NULL;         
    char* pch;                  
    pch = strtok(input, split_char);
    while (pch != NULL){                                                                                              
      if (i >= output_size){                                                                                           
        return i;                                                                                                      
      }//超过数组下标导致放不下                                                                                       
      output[i++] = pch;                                                                                             
      pch = strtok(NULL, split_char);                                                                                  
    }                                                                                                                
    return i;
}

int ParseQueryString(char querystring[], char* factory_name[], int* ItemNo){
    //把 querystring 按照字符串进行切分                                                                                
    char* tok[10];                              
    //把 querystring 按照 & = 进行字符串切分                                                                           
    //切分得到的每一个部分，就放到 tok 数组之中                                                                         
    //返回值，就是 tok 数组中包含几个元素         
    //最后一个参数 10 表示 tok 数组最多能放几个元素                                                                 
    int tok_size = Split(querystring, "=&", tok, 15);                                                                   
    if (tok_size != 4){//如果切分结果不等于4，就认为当前首行有问题                                            
      fprintf(stderr, "Split failed! tok_size=%d\n", tok_size);//打印错误日志                                         
      return -1;//表示切分失败                                                                                         
    }                                                         
    urldecode(tok[1]);            
    *factory_name = tok[1];   
    *ItemNo = atoi(tok[3]);
  
    //fprintf(stderr, "factory_name = %s\n", *factory_name);
    //fprintf(stderr, "color = %s\n", *color);      
    return 0;                                                                                                           
} 

int main(){
    // 0. 获取 query_string
    char buf[1024 * 4] = {0};
    if(GetQueryString(buf) < 0){
        fprintf(stderr, "GetQueryString failed\n");
        return 1;
    }
    char* factory_name = NULL;  
    int ItemNo = 0;            
    ParseQueryString(buf, &factory_name, &ItemNo);
    fprintf(stderr, "[CGI] factory_name=%s ItemNo=%d \n", factory_name, ItemNo);
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
            "zxy970922", "StockOut", 3306, NULL, 0);//真正的去连接数据库
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

    mysql_set_character_set(connect_fd, "utf8");

    // 2. 拼接sql语句
    //    组织命令
    char sql[1024 * 4] = {0};   

    fprintf(stderr, "[CGI22] factory_name=%s ItemNo=%d  \n", factory_name, ItemNo);                 
    sprintf(sql,               
          "select factory_name, ItemNo, t_date from Manifest where factory_name='%s' and ItemNo=%d",
           factory_name, ItemNo);//期望用户通过浏览器查询到的数据 

    // 3. 把sql语句发送到服务器
    //    使用遥控器把命令发给服务器
    int ret = mysql_query(connect_fd, sql);
    if(ret < 0){
        //说明出错，什么情况下会出错？
        //例如：语法错误
        fprintf(stderr, "mysql_query failed! %s\n", sql);
        return 1;
    }
     
    // 4. 读取并遍历服务器返回的结果
    MYSQL_RES* result = mysql_store_result(connect_fd);//结构体指针
    if(result == NULL){
        fprintf(stderr, "mysql_store_result failed!\n");
        return 1;
    }
    // a) 获取到表里面有几行几列
    int rows = mysql_num_rows(result);//行数
    int fields = mysql_num_fields(result);//列数
    // b) 获取到结果集合的表结构
    //怎么获取？
    MYSQL_FIELD* field = mysql_fetch_field(result);//获取表头的信息
    while(field != NULL){
        printf("%s\t", field->name);//将结果返回到客户端
        field = mysql_fetch_field(result);//获取表头的信息
    }
    printf("<br>");
    // c) 获取到每个元素的具体值
    int i = 0;
    for(; i < rows; ++i){
        MYSQL_ROW row = mysql_fetch_row(result);//第一次调获取到第一行，第二次调获取到第二行，依次类推
        int j = 0;
        for(; j < fields; ++j){
            printf("%s\t", row[j]);
        }
        printf("<br>");
    }
    printf("<br>");

    // 5. 断开连接
    mysql_close(connect_fd);
    return 0;
}
