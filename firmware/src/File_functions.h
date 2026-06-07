
#include <Arduino.h>
#include "LittleFS.h"

//====================================================================================
// Print LittleFS file system to directory
//====================================================================================
void dir(){
  int c=0;
  char serbuff[40];
  Serial.println("DIR>>");
  
  Dir dir = LittleFS.openDir("/");
  while (dir.next()) {
    if(dir.isDirectory()) Serial.print("|- ");
    sprintf(serbuff, "%s \t %d", dir.fileName().c_str(), dir.fileSize());
    Serial.println(serbuff);
    c++;
  }
  //sprintf(serbuff, "\n%d Files\n", c);
  Serial.println("<<EOD");
  //Serial.println(serbuff);
}

//====================================================================================
// Print LittleFS file system to directory
//====================================================================================
void clean(){
  Dir dir = LittleFS.openDir("/");
    while (dir.next()) {
        if(dir.isFile() && dir.fileSize()<2) {
          if(LittleFS.remove(dir.fileName())){
            Serial.print("Deleted ");
            Serial.print(dir.fileName());
          } else {
            Serial.println("Error");
          }
        }
    }
}

//====================================================================================
// Delete a file
//====================================================================================
void del(const char *filename){
    LittleFS.begin();
    Serial.print("Deleting "); Serial.println(filename);
    File myFile;
    if(!LittleFS.exists(filename)){
        Serial.println("File does not exist");
    }
    else if(LittleFS.remove(filename)){
        Serial.println("Deleted");
    } else {
        Serial.println("Error");
    }
}

//====================================================================================
// Print LittleFS file system to directory
//====================================================================================
void cat(const char *filename){
  File myFile;
  myFile = LittleFS.open(filename, "r");
  Serial.println(filename);
  Serial.println("");
  char c;
  if (myFile) {
    while (myFile.available()) {
      c = myFile.read();
      Serial.print(c);
    }
  } else {
    Serial.println("Error opening file");
  }
  myFile.close();
}

//====================================================================================
// Print LittleFS file system to directory
//====================================================================================
void ren(const char *from, const char *to){
  if(LittleFS.rename(from, to)){
    Serial.println("Renamed success");
  } else {
    Serial.println("Error renaming file");
  }
}


//====================================================================================
// 
//====================================================================================
void convertBin(const char *filename){

  Serial.print("Converting cache to ");
  Serial.println(filename);
  int bufferLength = 31;
  char buffer[bufferLength];
  char smBuff[3];
  int binByte;
  char *ptr;

  File readFile = LittleFS.open("cache.txt", "r");
  if(!readFile) Serial.println( "Cache file read failed");
  File writeFile = LittleFS.open(filename, "w");
  if(!writeFile) Serial.println( "Write file open failed");

  while(readFile.available()>0){
    readFile.readBytes(buffer, 30);
    Serial.println(buffer);
    for(int x=0; x<30; x+=2){
            smBuff[0] = buffer[x];
            smBuff[1] = buffer[x+1];
            binByte = strtol(smBuff, &ptr, 16);
            writeFile.write((byte)binByte);
        }
  }
  readFile.close();
  writeFile.close();

}