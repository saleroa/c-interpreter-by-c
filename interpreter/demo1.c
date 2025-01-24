#include<stdio.h>

int main(){
    int a;
    a = 0;
    if ((a >= 'a' && a <= 'z') || (a >= 'A' && a <= 'Z') || (a == '_')) {
      printf("nihao 1");
    }else{
        printf("hello 2");
    }
}