/* testPVdata.cpp */
/**
 * Copyright - See the COPYRIGHT that is included with this distribution.
 * EPICS pvDataCPP is distributed subject to a Software License Agreement found
 * in file LICENSE that is included with this distribution.
 */
/* Author:  Marty Kraimer Date: 2010.11 */

#include <cstddef>
#include <cstdlib>
#include <cstddef>
#include <string>
#include <cstdio>

#include <epicsAssert.h>

#include "requester.h"
#include "pvIntrospect.h"
#include "pvData.h"
#include "convert.h"
#include "standardField.h"
#include "standardPVField.h"
#include "alarm.h"
#include "control.h"
#include "display.h"
#include "timeStamp.h"
#include "pvAlarm.h"
#include "pvControl.h"
#include "pvDisplay.h"
#include "pvEnumerated.h"
#include "pvTimeStamp.h"
#include "showConstructDestruct.h"

using namespace epics::pvData;

static FieldCreate * fieldCreate = 0;
static PVDataCreate * pvDataCreate = 0;
static StandardField *standardField = 0;
static StandardPVField *standardPVField = 0;
static Convert *convert = 0;
static String builder("");
static String alarmTimeStamp("alarm,timeStamp");
static String allProperties("alarm,timeStamp,display,control");

static PVStructure *doubleRecord = 0;
static PVStructure *enumeratedRecord = 0;

static void createRecords(FILE * fd,FILE *auxfd)
{
    doubleRecord = standardPVField->scalarValue(0,pvDouble,allProperties);
    builder.clear();
    doubleRecord->toString(&builder);
    fprintf(fd,"%s\n",builder.c_str());
    String choices[4] = {
        String("0"),String("1"),String("2"),String("3")
    };
    enumeratedRecord = standardPVField->enumeratedValue(0,choices,4,alarmTimeStamp);
    builder.clear();
    enumeratedRecord->toString(&builder);
    fprintf(fd,"%s\n",builder.c_str());
}

static void deleteRecords(FILE * fd,FILE *auxfd)
{
    fprintf(fd,"doubleRecord\n");
    builder.clear();
    doubleRecord->toString(&builder);
    fprintf(fd,"%s\n",builder.c_str());
    fprintf(fd,"enumeratedRecord\n");
    builder.clear();
    enumeratedRecord->toString(&builder);
    fprintf(fd,"%s\n",builder.c_str());
    delete doubleRecord;
    delete enumeratedRecord;
}

static void testAlarm(FILE * fd,FILE *auxfd)
{
    fprintf(fd,"testAlarm\n");
    Alarm alarm;
    PVAlarm pvAlarm; 
    bool result;
    PVField *pvField = doubleRecord->getSubField(String("alarm"));
    if(pvField==0) {
        printf("testAlarm ERROR did not find field alarm\n");
        return;
    }
    result = pvAlarm.attach(pvField);
    assert(result);
    Alarm al;
    al.setMessage(String("testMessage"));
    al.setSeverity(majorAlarm);
    result = pvAlarm.set(al);
    assert(result);
    alarm = pvAlarm.get();
    assert(al.getMessage().compare(alarm.getMessage())==0);
    assert(al.getSeverity()==alarm.getSeverity());
    String message = alarm.getMessage();
    String severity = AlarmSeverityFunc::getSeverityNames()[alarm.getSeverity()];
    fprintf(fd," message %s severity %s\n",message.c_str(),severity.c_str());
}

static void testTimeStamp(FILE * fd,FILE *auxfd)
{
    fprintf(fd,"testTimeStamp\n");
    TimeStamp timeStamp;
    PVTimeStamp pvTimeStamp; 
    bool result;
    PVField *pvField = doubleRecord->getSubField(String("timeStamp"));
    if(pvField==0) {
        printf("testTimeStamp ERROR did not find field timeStamp\n");
        return;
    }
    result = pvTimeStamp.attach(pvField);
    assert(result);
    TimeStamp ts;
    ts.getCurrent();
    result = pvTimeStamp.set(ts);
    assert(result);
    timeStamp = pvTimeStamp.get();
    assert(ts.getSecondsPastEpoch()==timeStamp.getSecondsPastEpoch());
    assert(ts.getNanoSeconds()==timeStamp.getNanoSeconds());
    time_t tt;
    timeStamp.toTime_t(tt);
    struct tm ctm;
    memcpy(&ctm,localtime(&tt),sizeof(struct tm));
    fprintf(auxfd,
        "%4.4d.%2.2d.%2.2d %2.2d:%2.2d:%2.2d %d nanoSeconds isDst %s\n",
        ctm.tm_year+1900,ctm.tm_mon + 1,ctm.tm_mday,
        ctm.tm_hour,ctm.tm_min,ctm.tm_sec,
        timeStamp.getNanoSeconds(),
        (ctm.tm_isdst==0) ? "false" : "true");
    timeStamp.put(0,0);
    pvTimeStamp.set(timeStamp);
}

static void testControl(FILE * fd,FILE *auxfd)
{
    fprintf(fd,"testControl\n");
    Control control;
    PVControl pvControl; 
    bool result;
    PVField *pvField = doubleRecord->getSubField(String("control"));
    if(pvField==0) {
        printf("testControl ERROR did not find field control\n");
        return;
    }
    result = pvControl.attach(pvField);
    assert(result);
    Control cl;
    cl.setLow(1.0);
    cl.setHigh(10.0);
    result = pvControl.set(cl);
    assert(result);
    control = pvControl.get();
    assert(cl.getLow()==control.getLow());
    assert(cl.getHigh()==control.getHigh());
    double low = control.getLow();
    double high = control.getHigh();
    fprintf(fd," low %f high %f\n",low,high);
}

static void testDisplay(FILE * fd,FILE *auxfd)
{
    fprintf(fd,"testDisplay\n");
    Display display;
    PVDisplay pvDisplay; 
    bool result;
    PVField *pvField = doubleRecord->getSubField(String("display"));
    if(pvField==0) {
        printf("testDisplay ERROR did not find field display\n");
        return;
    }
    result = pvDisplay.attach(pvField);
    assert(result);
    Display dy;
    dy.setLow(-10.0);
    dy.setHigh(-1.0);
    dy.setDescription(String("testDescription"));
    dy.setFormat(String("%f10.0"));
    dy.setUnits(String("volts"));
    result = pvDisplay.set(dy);
    assert(result);
    display = pvDisplay.get();
    assert(dy.getLow()==display.getLow());
    assert(dy.getHigh()==display.getHigh());
    assert(dy.getDescription().compare(display.getDescription())==0);
    assert(dy.getFormat().compare(display.getFormat())==0);
    assert(dy.getUnits().compare(display.getUnits())==0);
    double low = display.getLow();
    double high = display.getHigh();
    fprintf(fd," low %f high %f\n",low,high);
}

static void testEnumerated(FILE * fd,FILE *auxfd)
{
    fprintf(fd,"testEnumerated\n");
    PVEnumerated pvEnumerated; 
    bool result;
    PVField *pvField = enumeratedRecord->getSubField(String("value"));
    if(pvField==0) {
        printf("testEnumerated ERROR did not find field enumerated\n");
        return;
    }
    result = pvEnumerated.attach(pvField);
    assert(result);
    int32 index = pvEnumerated.getIndex();
    String choice = pvEnumerated.getChoice();
    StringArray choices = pvEnumerated.getChoices();
    int32 numChoices = pvEnumerated.getNumberChoices();
    fprintf(fd,"index %d choice %s choices",index,choice.c_str());
    for(int i=0; i<numChoices; i++ ) fprintf(fd," %s",choices[i].c_str());
    fprintf(fd,"\n");
    pvEnumerated.setIndex(2);
    index = pvEnumerated.getIndex();
    choice = pvEnumerated.getChoice();
    fprintf(fd,"index %d choice %s\n",index,choice.c_str());
}

int main(int argc,char *argv[])
{
    char *fileName = 0;
    if(argc>1) fileName = argv[1];
    FILE * fd = stdout;
    if(fileName!=0 && fileName[0]!=0) {
        fd = fopen(fileName,"w+");
    }
    char *auxFileName = 0;
    if(argc>2) auxFileName = argv[2];
    FILE *auxfd = stdout;
    if(auxFileName!=0 && auxFileName[0]!=0) {
        auxfd = fopen(auxFileName,"w+");
    }
    fieldCreate = getFieldCreate();
    pvDataCreate = getPVDataCreate();
    standardField = getStandardField();
    standardPVField = getStandardPVField();
    convert = getConvert();
    createRecords(fd,auxfd);
    testAlarm(fd,auxfd);
    testTimeStamp(fd,auxfd);
    testControl(fd,auxfd);
    testDisplay(fd,auxfd);
    testEnumerated(fd,auxfd);
    deleteRecords(fd,auxfd);
    getShowConstructDestruct()->constuctDestructTotals(fd);
    return(0);
}

