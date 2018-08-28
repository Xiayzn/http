#pragma once
#define SIZE 10240

typedef struct Request{
    char first_line[SIZE];//首行 first_line(方法 method，url ，版本号)
    char* method;//方法 method
    char* url;//url
    char* url_path;//重点关注的内容1. path路径
    char* query_string;//重点关注的内容2. query_string 参数
    //char* version; 版本号
    //接下来是header部分，如果要完整的解析下来，
    //此处需要使用二叉搜索树或者哈希表，
    //这里我们偷个懒，其他header我们都不要了只保留一个content_length
    int content_length;
}Request;
