/* Convert.cpp */
#include <cstdio>
#include <string>
#include <vector>
#include <stdexcept>
#include <epicsTypes.h>
#include "lock.h"
#include "pvIntrospect.h"
#include "pvData.h"
#include "convert.h"

namespace epics { namespace pvData { 

static Convert* convert = 0;

static String logicError("Logic error. Should never get here.");
static String notImplemented("not implemented");
static String illegalScalarType("Illegal ScalarType");

static bool convertEquals(PVField *a,PVField *b);
static int convertFromByteArray(PVScalarArray *pv, int offset,
    int len,epicsInt8 from[], int fromOffset);
static int convertToByteArray(PVScalarArray *pv, int offset,
    int len,epicsInt8 to[], int toOffset);
static int convertFromShortArray(PVScalarArray *pv, int offset,
    int len,epicsInt16 from[], int fromOffset);
static int convertToShortArray(PVScalarArray *pv, int offset,
    int len,epicsInt16 to[], int toOffset);
static int convertFromIntArray(PVScalarArray *pv, int offset,
    int len,epicsInt32 from[], int fromOffset);
static int convertToIntArray(PVScalarArray *pv, int offset,
    int len,epicsInt32 to[], int toOffset);
static int convertFromLongArray(PVScalarArray *pv, int offset,
    int len,epicsInt64 from[], int fromOffset);
static int convertToLongArray(PVScalarArray * pv, int offset,
    int len,epicsInt64 to[], int toOffset);
static int convertFromFloatArray(PVScalarArray *pv, int offset,
    int len,float from[], int fromOffset);
static int convertToFloatArray(PVScalarArray * pv, int offset,
    int len,float to[], int toOffset);
static int convertFromDoubleArray(PVScalarArray *pv, int offset,
    int len,double from[], int fromOffset);
static int convertToDoubleArray(PVScalarArray * pv, int offset,
    int len,double to[], int toOffset);
static int convertFromStringArray(PVScalarArray *pv, int offset,
    int len,String from[], int fromOffset);
static int convertToStringArray(PVScalarArray * pv, int offset,
    int len,String to[], int toOffset);

static void convertToString(StringBuilder buffer,
    PVField * pv,int indentLevel);
static void convertStructure(StringBuilder buffer,
    PVStructure *data,int indentLevel);
static void convertArray(StringBuilder buffer,
    PVScalarArray * pv,int indentLevel);
static void convertStructureArray(StringBuilder buffer,
    PVStructureArray * pvdata,int indentLevel);
static int copyArrayDataReference(PVScalarArray *from,PVArray *to);
static int CopyNumericArray(PVScalarArray *from,
    int offset, PVScalarArray *to, int toOffset, int len);

static std::vector<String> split(String commaSeparatedList);

static std::vector<String> split(String commaSeparatedList) {
    String::size_type numValues = 1;
    String::size_type index=0;
    while(epicsTrue) {
        String::size_type pos = commaSeparatedList.find(',',index);
        if(pos==String::npos) break;
        numValues++;
        index = pos +1;
    }
    std::vector<String> valueList(numValues,"");
    index=0;
    for(size_t i=0; i<numValues; i++) {
        int pos = commaSeparatedList.find(',',index);
        String value = commaSeparatedList.substr(index,pos);
        valueList[i] = value;
        index = pos +1;
    }
    return valueList;
}
    
Convert::Convert(){}

Convert::~Convert(){}

void Convert::getFullName(StringBuilder buf,PVField * pvField)
{
    buf->empty();
    *buf += pvField->getField()->getFieldName();
    PVStructure *parent;
    while((parent=pvField->getParent())!=0) {
         pvField = pvField->getParent();
         String name = pvField->getField()->getFieldName();
         if(name.length()>0) {
             buf->insert(0,".");
             buf->insert(0,name);
         }
    }
}

bool Convert::equals(PVField *a,PVField *b)
{
    return convertEquals(a,b);
}

void Convert::getString(StringBuilder buf,PVField * pvField,int indentLevel)
{
    convertToString(buf,pvField,indentLevel);
}

void Convert::getString(StringBuilder buf,PVField * pvField)
{
    convertToString(buf,pvField,0);
}

void Convert::fromString(PVScalar *pvScalar, String from)
{
    ScalarConstPtr scalar = pvScalar->getScalar();
    ScalarType scalarType = scalar->getScalarType();
    switch(scalarType) {
        case pvBoolean: {
                PVBoolean *pv = (PVBoolean *)pvScalar;
                bool value = 
                  ((from.compare("true")==0) ? epicsTrue : epicsFalse);
                pv->put(value);
                break;
            }
        case pvByte : {
                PVByte *pv = (PVByte*)pvScalar;
                int ival;
                sscanf(from.c_str(),"%d",&ival);
                epicsInt8 value = ival;
                pv->put(value);
                break;
            }
        case pvShort : {
                PVShort *pv = (PVShort*)pvScalar;
                int ival;
                sscanf(from.c_str(),"%d",&ival);
                epicsInt16 value = ival;
                pv->put(value);
                break;
            }
        case pvInt : {
                PVInt *pv = (PVInt*)pvScalar;
                int ival;
                sscanf(from.c_str(),"%d",&ival);
                epicsInt32 value = ival;
                pv->put(value);
                break;
            }
        case pvLong : {
                PVLong *pv = (PVLong*)pvScalar;
                long int ival;
                sscanf(from.c_str(),"%ld",&ival);
                epicsInt64 value = ival;
                pv->put(value);
                break;
            }
        case pvFloat : {
                PVFloat *pv = (PVFloat*)pvScalar;
                float value;
                sscanf(from.c_str(),"%f",&value);
                pv->put(value);
                break;
            }
        case pvDouble : {
                PVDouble*pv = (PVDouble*)pvScalar;
                double value;
                sscanf(from.c_str(),"%lf",&value);
                pv->put(value);
                break;
            }
        case pvString: {
                PVString *value = (PVString*)pvScalar;
                value->put(from);
                break;
            }
    }
    String message("Convert::fromString unknown scalarType ");
    ScalarTypeFunc::toString(&message,scalarType);
    throw std::logic_error(message);
}

int Convert::fromString(PVScalarArray *pv, String from)
{
   if(from[0]=='[' && from[from.length()]==']') {
        int offset = from.rfind(']');
        from = from.substr(1, offset);
    }
    std::vector<String> valueList = split(from);
    int length = valueList.size();
    StringArray valueArray = new String[length];
    for(int i=0; i<length; i++) {
        String value = valueList[i];
        valueArray[i] = value;
    }
    int num = fromStringArray(pv,0,length,valueArray,0);
    if(num<length) length = num;
    pv->setLength(length);
    delete[] valueArray;
    return length;
}

int Convert::fromStringArray(PVScalarArray *pv, int offset, int length,
    String from[], int fromOffset)
{
    return convertFromStringArray(pv,offset,length,from,fromOffset);
}

int Convert::toStringArray(PVScalarArray * pv, int offset, int length,
    String to[], int toOffset)
{
    return convertToStringArray(pv,offset,length,to,toOffset);
}

bool Convert::isCopyCompatible(FieldConstPtr from, FieldConstPtr to)
{
    if(from->getType()!=to->getType()) return epicsFalse;
    switch(from->getType()) {
    case scalar: 
        return isCopyScalarCompatible((ScalarConstPtr)from,(ScalarConstPtr)to);
    case scalarArray:
        return isCopyScalarArrayCompatible((ScalarArrayConstPtr)from,(ScalarArrayConstPtr)to);
    case structure:
        return isCopyStructureCompatible((StructureConstPtr)from,(StructureConstPtr)to);
    case structureArray:
        return isCopyStructureArrayCompatible((StructureArrayConstPtr)from,(StructureArrayConstPtr)to);
    }
    String message("Convert::isCopyCompatible should never get here");
    throw std::logic_error(message);
}

void Convert::copy(PVField *from,PVField *to)
{
    switch(from->getField()->getType()) {
    case scalar: 
        copyScalar((PVScalar *)from,(PVScalar *)to);
        return;
    case scalarArray: {
        PVScalarArray  *fromArray = (PVScalarArray *)from;
        PVScalarArray  *toArray = (PVScalarArray *)to;
        int length = copyScalarArray(fromArray,0,toArray,0,fromArray->getLength());
        if(toArray->getLength()!=length) toArray->setLength(length);
        return;
    }
    case structure:
        copyStructure((PVStructure *)from,(PVStructure *)to);
        return;
    case structureArray: {
    	PVStructureArray  *fromArray = (PVStructureArray *)from;
        PVStructureArray  *toArray = (PVStructureArray *)to;
        copyStructureArray(fromArray,toArray);
        return;
    }
    }
}

bool Convert::isCopyScalarCompatible(
     ScalarConstPtr fromField, ScalarConstPtr toField)
{
    ScalarType fromScalarType = fromField->getScalarType();
    ScalarType toScalarType = toField->getScalarType();
    if(fromScalarType==toScalarType) return epicsTrue;
    if(ScalarTypeFunc::isNumeric(fromScalarType)
    && ScalarTypeFunc::isNumeric(toScalarType)) return epicsTrue;
    if(fromScalarType==pvString) return epicsTrue;
    if(toScalarType==pvString) return epicsTrue;
    return epicsFalse;
}

void Convert::copyScalar(PVScalar *from, PVScalar *to)
{
    if(to->isImmutable()) {
        if(from==to) return;
        String message("Convert.copyScalar destination is immutable");
        throw std::invalid_argument(message);
    }
    ScalarType fromType = from->getScalar()->getScalarType();
    ScalarType toType = to->getScalar()->getScalarType();
    switch(fromType) {
    case pvBoolean: {
            if(toType!=pvBoolean) {
                if(toType!=pvString) {
                    String message("Convert.copyScalar arguments are not compatible");
                    throw std::invalid_argument(message);
                }
            }
            PVBoolean *data = (PVBoolean*)from;
            if(toType==pvString) {
                PVString *dataTo = (PVString*)to;
                String buf("");
                data->toString(&buf);
                dataTo->put(buf);
            } else {
                bool value = data->get();
                PVBoolean *dataTo = (PVBoolean*)to;
                dataTo->put(value);
            }
            break;
        }
    case pvByte : {
            PVByte *data = (PVByte*)from;
            epicsInt8 value = data->get();
            convert->fromByte(to,value);
            break;
        }
    case pvShort : {
            PVShort *data = (PVShort*)from;
            short value = data->get();
            convert->fromShort(to,value);
            break;
        } 
    case pvInt :{
            PVInt *data = (PVInt*)from;
            int value = data->get();
            convert->fromInt(to,value);
            break;
        }    
    case pvLong : {
            PVLong *data = (PVLong*)from;
            long value = data->get();
            convert->fromLong(to,value);
            break;
        }  
    case pvFloat : {
            PVFloat *data = (PVFloat*)from;
            float value = data->get();
            convert->fromFloat(to,value);
            break;
        }     
    case pvDouble : {
            PVDouble *data = (PVDouble*)from;
            double value = data->get();
            convert->fromDouble(to,value);
            break;
        }  
    case pvString: {
            PVString *data = (PVString*)from;
            String value = data->get();
            convert->fromString(to,value);
            break;
        }
    }
    String message("Convert::copyScalar should never get here");
    throw std::logic_error(message);
}

bool Convert::isCopyScalarArrayCompatible(ScalarArrayConstPtr fromArray,
    ScalarArrayConstPtr toArray)
{
    ScalarType fromType = fromArray->getElementType();
    ScalarType toType = toArray->getElementType();
    if(fromType==toType) return epicsTrue;
    if(ScalarTypeFunc::isNumeric(fromType)
    && ScalarTypeFunc::isNumeric(toType)) return epicsTrue;
    if(toType==pvString) return epicsTrue;
    if(fromType==pvString) return epicsTrue;
    return epicsFalse;
}

int Convert::copyScalarArray(PVScalarArray *from, int offset,
    PVScalarArray *to, int toOffset, int length)
{
    if(to->isImmutable()) {
        if(from==to) return from->getLength();
        String message("Convert.copyArray destination is immutable");
        throw std::invalid_argument(message);
    }
    ScalarType fromElementType = from->getScalarArray()->getElementType();
    ScalarType toElementType = to->getScalarArray()->getElementType();
    
    if(from->isImmutable() && (fromElementType==toElementType)) {
        if(offset==0 && toOffset==0 && length==from->getLength()) {
            return copyArrayDataReference(from,to);
        }
    }
    
    int ncopy = 0;
    if(ScalarTypeFunc::isNumeric(fromElementType)
    && ScalarTypeFunc::isNumeric(toElementType)) {
        return CopyNumericArray(from,offset,to,toOffset,length);
    } else if(toElementType==pvBoolean && fromElementType==pvBoolean) {
        PVBooleanArray *pvfrom = (PVBooleanArray*)from;
        PVBooleanArray *pvto = (PVBooleanArray*)to;
        while(length>0) {
            int num = 0;
            bool *data = 0;
            int fromOffset = 0;
            BooleanArrayData booleanArrayData = BooleanArrayData();
            num = pvfrom->get(offset,length,&booleanArrayData);
            data = booleanArrayData.data;
            fromOffset = booleanArrayData.offset;
            if(num<=0) return ncopy;
            while(num>0) {
                int n = pvto->put(toOffset,num,data,fromOffset);
                if(n<=0) return ncopy;
                length -= n; num -= n; ncopy+=n; offset += n; toOffset += n; 
            }
        }
    } else if(toElementType==pvString && fromElementType==pvString) {
        PVStringArray *pvfrom = (PVStringArray*)from;
        PVStringArray *pvto = (PVStringArray*)to;
        while(length>0) {
            int num = 0;
            String *data;
            int fromOffset = 0;
            StringArrayData stringArrayData = StringArrayData();
            num = pvfrom->get(offset,length,&stringArrayData);
            data = stringArrayData.data;
            fromOffset = stringArrayData.offset;
            if(num<=0) return ncopy;
            while(num>0) {
                int n = pvto->put(toOffset,num,data,fromOffset);
                if(n<=0) return ncopy;
                length -= n; num -= n; ncopy+=n; offset += n; toOffset += n; 
            }
        }
    } else if(toElementType==pvString) {
        PVStringArray *pvto = (PVStringArray*)to;
        ncopy = from->getLength();
        if(ncopy>length) ncopy = length;
        int num = ncopy;
        String toData[1];
        while(num>0) {
            convert->toStringArray(from,offset,1,toData,0);
            if(pvto->put(toOffset,1,toData,0)<=0) break;
            num--; offset++; toOffset++;
        }
        return ncopy;
    } else if(fromElementType==pvString) {
        PVStringArray *pvfrom = (PVStringArray*)from;
        while(length>0) {
            int num = 0;
            String *data = 0;
            int fromOffset = 0;
            StringArrayData stringArrayData = StringArrayData();
            num = pvfrom->get(offset,length,&stringArrayData);
            data = stringArrayData.data;
            fromOffset = stringArrayData.offset;
            if(num<=0) return ncopy;
            while(num>0) {
                int n = fromStringArray(to,toOffset,num,data,fromOffset);
                if(n<=0) return ncopy;
                length -= n; num -= n; ncopy+=n; offset += n; toOffset += n; 
            }
        }
    }
    String message("Convert::copyScalarArray should not get here");
    throw std::logic_error(message);
}

bool Convert::isCopyStructureCompatible(
    StructureConstPtr fromStruct, StructureConstPtr toStruct)
{
    FieldConstPtrArray fromFields = fromStruct->getFields();
    FieldConstPtrArray toFields = toStruct->getFields();
    int length = fromStruct->getNumberFields();
    if(length!=toStruct->getNumberFields()) return epicsFalse;
    for(int i=0; i<length; i++) {
        FieldConstPtr from = fromFields[i];
        FieldConstPtr to = toFields[i];
        Type fromType = from->getType();
        Type toType = to->getType();
        if(fromType!=toType) return epicsFalse;
        switch(fromType) {
        case scalar:
            if(!convert->isCopyScalarCompatible((ScalarConstPtr)from,(ScalarConstPtr)to)) return epicsFalse;
            break;
        case scalarArray:
            if(!isCopyScalarArrayCompatible((ScalarArrayConstPtr)from,(ScalarArrayConstPtr)to))
                return epicsFalse;
            break;
        case structure:
            if(!isCopyStructureCompatible((StructureConstPtr)from,(StructureConstPtr)to))
                return epicsFalse;
            break;
        case structureArray:
            if(!isCopyStructureArrayCompatible((StructureArrayConstPtr)from,
                (StructureArrayConstPtr)to)) return epicsFalse;
        }
    }
    return epicsTrue;
}

void Convert::copyStructure(PVStructure *from, PVStructure *to)
{
    if(to->isImmutable()) {
        if(from==to) return;
        String message("Convert.copyStructure destination is immutable");
        throw std::invalid_argument(message);
    }
    if(from==to) return;
    PVFieldPtrArray fromDatas = from->getPVFields();
    PVFieldPtrArray toDatas = to->getPVFields();
    if(from->getStructure()->getNumberFields() 
    != to->getStructure()->getNumberFields()) {
        String message("Convert.copyStructure Illegal copyStructure");
        throw std::invalid_argument(message);
    }
    int numberFields = from->getStructure()->getNumberFields();
    if(numberFields==2) {
        // look for enumerated structure and copy choices first
        String fieldName = fromDatas[0]->getField()->getFieldName();
        if(fieldName.compare("index")==0) {
            FieldConstPtr fieldIndex = fromDatas[0]->getField();
            FieldConstPtr fieldChoices = fromDatas[1]->getField();
            if(fieldIndex->getType()==scalar
            && fieldChoices->getFieldName().compare("choices")
            && fieldChoices->getType()==scalarArray) {
                PVScalar *pvScalar = (PVScalar*)fromDatas[0];
                PVScalarArray *pvArray = (PVScalarArray*)fromDatas[1];
                if((pvScalar->getScalar()->getScalarType()==pvInt)
                && (pvArray->getScalarArray()->getElementType()==pvString)) {
                   PVScalarArray* toArray = (PVScalarArray*)toDatas[1];
                   copyScalarArray(pvArray,0,toArray,0,pvArray->getLength());
                   PVScalar *toScalar = (PVScalar*)toDatas[0];
                   copyScalar(pvScalar,toScalar);
                   return;
                }
            }
        }
    }
    for(int i=0; i < numberFields; i++) {
        PVField *fromData = fromDatas[i];
        PVField *toData = toDatas[i];
        Type fromType = fromData->getField()->getType();
        Type toType = toData->getField()->getType();
        if(fromType!=toType) {
            String message("Convert.copyStructure Illegal copyStructure");
            throw std::invalid_argument(message);
        }
        switch(fromType) {
        case scalar:
            copyScalar((PVScalar*)fromData,(PVScalar*)toData);
            break;
        case scalarArray: {
            PVScalarArray *fromArray = (PVScalarArray*)fromData;
            PVScalarArray *toArray = (PVScalarArray*)toData;
            int length = copyScalarArray(fromArray,0,toArray,0,fromArray->getLength());
            if(toArray->getLength()!=length) toArray->setLength(length);
            break;
        }
        case structure:
            copyStructure((PVStructure*)fromData,(PVStructure*)toData);
            break;
        case structureArray: {
        	PVStructureArray *fromArray = (PVStructureArray*)fromData;
            PVStructureArray *toArray = (PVStructureArray*)toData;
            copyStructureArray(fromArray,toArray);
            break;
        }
        }
    }
}

bool Convert::isCopyStructureArrayCompatible(
    StructureArrayConstPtr from, StructureArrayConstPtr to)
{
    return isCopyStructureCompatible(from->getStructure(),to->getStructure());
}

void Convert::copyStructureArray(
    PVStructureArray *from, PVStructureArray *to)
{
    if(to->isImmutable()) {
        if(from==to) return;
        String message("Convert.copyStructureArray destination is immutable");
        throw std::invalid_argument(message);
    }
    if(!isCopyStructureCompatible(
    from->getStructureArray()->getStructure(),
    to->getStructureArray()->getStructure())) {
        String message("Convert.copyStructureArray from and to are not compatible");
        throw std::invalid_argument(message);
    }
    PVStructurePtrArray fromArray = 0;
    int length = from->getLength();
    StructureArrayData structureArrayData = StructureArrayData();
    from->get(0, length,&structureArrayData);
    fromArray = structureArrayData.data;
    PVStructurePtrArray toArray = 0;
    if(to->getCapacity()<length) to->setCapacity(length);
    to->get(0, length,&structureArrayData);
    toArray = structureArrayData.data;
    for(int i=0; i<length; i++) {
    	if(fromArray[i]==0) {
            toArray[i] = 0;
    	} else {
    	    if(toArray[i]==0) {
    		StructureConstPtr structure = to->getStructureArray()->getStructure();
    		toArray[i] = getPVDataCreate()->createPVStructure(0,structure);
    	    }
    	    copyStructure(fromArray[i],toArray[i]);
    	}
    }
    to->setLength(length);
    to->postPut();
}

epicsInt8 Convert::toByte(PVScalar * pv)
{
    ScalarConstPtr scalar = pv->getScalar();
    ScalarType scalarType = scalar->getScalarType();
    switch(scalarType) {
        case pvBoolean:
           throw std::logic_error(String("boolean can not be converted to byte"));
        case pvByte: {
            PVByte *value = (PVByte *)pv;
            return value->get();
        }
        case pvShort: {
            PVShort *value = (PVShort *)pv;
            return value->get();
        }
        case pvInt: {
            PVInt *value = (PVInt *)pv;
            return value->get();
        }
        case pvLong: {
            PVLong *value = (PVLong *)pv;
            return value->get();
        }
        case pvFloat: {
            PVFloat *value = (PVFloat *)pv;
            return value->get();
        }
        case pvDouble: {
            PVDouble *value = (PVDouble *)pv;
            return value->get();
        }
        case pvString:
           throw std::logic_error(String("string can not be converted to byte"));
    }
    throw std::logic_error(logicError);
}

epicsInt16 Convert::toShort(PVScalar * pv)
{
    ScalarConstPtr scalar = pv->getScalar();
    ScalarType scalarType = scalar->getScalarType();
    switch(scalarType) {
        case pvBoolean:
           throw std::logic_error(String("boolean can not be converted to short"));
        case pvByte: {
            PVByte *value = (PVByte *)pv;
            return value->get();
        }
        case pvShort: {
            PVShort *value = (PVShort *)pv;
            return value->get();
        }
        case pvInt: {
            PVInt *value = (PVInt *)pv;
            return value->get();
        }
        case pvLong: {
            PVLong *value = (PVLong *)pv;
            return value->get();
        }
        case pvFloat: {
            PVFloat *value = (PVFloat *)pv;
            return value->get();
        }
        case pvDouble: {
            PVDouble *value = (PVDouble *)pv;
            return value->get();
        }
        case pvString:
           throw std::logic_error(String("string can not be converted to short"));
    }
    throw std::logic_error(logicError);
}

epicsInt32 Convert::toInt(PVScalar * pv)
{
    ScalarConstPtr scalar = pv->getScalar();
    ScalarType scalarType = scalar->getScalarType();
    switch(scalarType) {
        case pvBoolean:
           throw std::logic_error(String("boolean can not be converted to int"));
        case pvByte: {
            PVByte *value = (PVByte *)pv;
            return value->get();
        }
        case pvShort: {
            PVShort *value = (PVShort *)pv;
            return value->get();
        }
        case pvInt: {
            PVInt *value = (PVInt *)pv;
            return value->get();
        }
        case pvLong: {
            PVLong *value = (PVLong *)pv;
            return value->get();
        }
        case pvFloat: {
            PVFloat *value = (PVFloat *)pv;
            return value->get();
        }
        case pvDouble: {
            PVDouble *value = (PVDouble *)pv;
            return value->get();
        }
        case pvString:
           throw std::logic_error(String("string can not be converted to int"));
    }
    throw std::logic_error(logicError);
}

epicsInt64 Convert::toLong(PVScalar * pv)
{
    ScalarConstPtr scalar = pv->getScalar();
    ScalarType scalarType = scalar->getScalarType();
    switch(scalarType) {
        case pvBoolean:
           throw std::logic_error(String("boolean can not be converted to long"));
        case pvByte: {
            PVByte *value = (PVByte *)pv;
            return value->get();
        }
        case pvShort: {
            PVShort *value = (PVShort *)pv;
            return value->get();
        }
        case pvInt: {
            PVInt *value = (PVInt *)pv;
            return value->get();
        }
        case pvLong: {
            PVLong *value = (PVLong *)pv;
            return value->get();
        }
        case pvFloat: {
            PVFloat *value = (PVFloat *)pv;
            return value->get();
        }
        case pvDouble: {
            PVDouble *value = (PVDouble *)pv;
            return value->get();
        }
        case pvString:
           throw std::logic_error(String("string can not be converted to long"));
    }
    throw std::logic_error(logicError);
}

float Convert::toFloat(PVScalar * pv)
{
    ScalarConstPtr scalar = pv->getScalar();
    ScalarType scalarType = scalar->getScalarType();
    switch(scalarType) {
        case pvBoolean:
           throw std::logic_error(String("boolean can not be converted to float"));
        case pvByte: {
            PVByte *value = (PVByte *)pv;
            return value->get();
        }
        case pvShort: {
            PVShort *value = (PVShort *)pv;
            return value->get();
        }
        case pvInt: {
            PVInt *value = (PVInt *)pv;
            return value->get();
        }
        case pvLong: {
            PVLong *value = (PVLong *)pv;
            return value->get();
        }
        case pvFloat: {
            PVFloat *value = (PVFloat *)pv;
            return value->get();
        }
        case pvDouble: {
            PVDouble *value = (PVDouble *)pv;
            return value->get();
        }
        case pvString:
           throw std::logic_error(String("string can not be converted to float"));
    }
    throw std::logic_error(logicError);
}

double Convert::toDouble(PVScalar * pv)
{
    ScalarConstPtr scalar = pv->getScalar();
    ScalarType scalarType = scalar->getScalarType();
    switch(scalarType) {
        case pvBoolean:
           throw std::logic_error(String("boolean can not be converted to double"));
        case pvByte: {
            PVByte *value = (PVByte *)pv;
            return value->get();
        }
        case pvShort: {
            PVShort *value = (PVShort *)pv;
            return value->get();
        }
        case pvInt: {
            PVInt *value = (PVInt *)pv;
            return value->get();
        }
        case pvLong: {
            PVLong *value = (PVLong *)pv;
            return value->get();
        }
        case pvFloat: {
            PVFloat *value = (PVFloat *)pv;
            return value->get();
        }
        case pvDouble: {
            PVDouble *value = (PVDouble *)pv;
            return value->get();
        }
        case pvString:
           throw std::logic_error(String("string can not be converted to double"));
    }
    throw std::logic_error(logicError);
}

void Convert::fromByte(PVScalar *pv,epicsInt8 from)
{
    ScalarConstPtr scalar = pv->getScalar();
    ScalarType scalarType = scalar->getScalarType();
    switch(scalarType) {
        case pvBoolean:
           throw std::logic_error(String("byte can not be converted to boolean"));
        case pvByte: {
            PVByte *value = (PVByte *)pv;
            value->put(from); return;
        }
        case pvShort: {
            PVShort *value = (PVShort *)pv;
            value->put(from); return;
        }
        case pvInt: {
            PVInt *value = (PVInt *)pv;
            value->put(from); return;
        }
        case pvLong: {
            PVLong *value = (PVLong *)pv;
            value->put(from); return;
        }
        case pvFloat: {
            PVFloat *value = (PVFloat *)pv;
            value->put(from); return;
        }
        case pvDouble: {
            PVDouble *value = (PVDouble *)pv;
            value->put(from); return;
        }
        case pvString: {
            PVString *value = (PVString *)pv;
            char buffer[20];
            int ival = from;
            sprintf(buffer,"%d",ival);
            String xxx(buffer);
            value->put(xxx);
            return;
        }
    }
    throw std::logic_error(logicError);
}

void  Convert::fromShort(PVScalar *pv,epicsInt16 from)
{
    ScalarConstPtr scalar = pv->getScalar();
    ScalarType scalarType = scalar->getScalarType();
    switch(scalarType) {
        case pvBoolean:
           throw std::logic_error(String("short can not be converted to boolean"));
        case pvByte: {
            PVByte *value = (PVByte *)pv;
            value->put(from); return;
        }
        case pvShort: {
            PVShort *value = (PVShort *)pv;
            value->put(from); return;
        }
        case pvInt: {
            PVInt *value = (PVInt *)pv;
            value->put(from); return;
        }
        case pvLong: {
            PVLong *value = (PVLong *)pv;
            value->put(from); return;
        }
        case pvFloat: {
            PVFloat *value = (PVFloat *)pv;
            value->put(from); return;
        }
        case pvDouble: {
            PVDouble *value = (PVDouble *)pv;
            value->put(from); return;
        }
        case pvString: {
            PVString *value = (PVString *)pv;
            char buffer[20];
            int ival = from;
            sprintf(buffer,"%d",ival);
            String xxx(buffer);
            value->put(xxx);
            return;
        }
    }
    throw std::logic_error(logicError);
}

void  Convert::fromInt(PVScalar *pv, epicsInt32 from)
{
    ScalarConstPtr scalar = pv->getScalar();
    ScalarType scalarType = scalar->getScalarType();
    switch(scalarType) {
        case pvBoolean:
           throw std::logic_error(String("int can not be converted to boolean"));
        case pvByte: {
            PVByte *value = (PVByte *)pv;
            value->put(from); return;
        }
        case pvShort: {
            PVShort *value = (PVShort *)pv;
            value->put(from); return;
        }
        case pvInt: {
            PVInt *value = (PVInt *)pv;
            value->put(from); return;
        }
        case pvLong: {
            PVLong *value = (PVLong *)pv;
            value->put(from); return;
        }
        case pvFloat: {
            PVFloat *value = (PVFloat *)pv;
            value->put(from); return;
        }
        case pvDouble: {
            PVDouble *value = (PVDouble *)pv;
            value->put(from); return;
        }
        case pvString: {
            PVString *value = (PVString *)pv;
            char buffer[20];
            int ival = from;
            sprintf(buffer,"%d",ival);
            String xxx(buffer);
            value->put(xxx);
            return;
        }
    }
    throw std::logic_error(logicError);
}

void  Convert::fromLong(PVScalar *pv, epicsInt64 from)
{
    ScalarConstPtr scalar = pv->getScalar();
    ScalarType scalarType = scalar->getScalarType();
    switch(scalarType) {
        case pvBoolean:
           throw std::logic_error(String("long can not be converted to boolean"));
        case pvByte: {
            PVByte *value = (PVByte *)pv;
            value->put(from); return;
        }
        case pvShort: {
            PVShort *value = (PVShort *)pv;
            value->put(from); return;
        }
        case pvInt: {
            PVInt *value = (PVInt *)pv;
            value->put(from); return;
        }
        case pvLong: {
            PVLong *value = (PVLong *)pv;
            value->put(from); return;
        }
        case pvFloat: {
            PVFloat *value = (PVFloat *)pv;
            value->put(from); return;
        }
        case pvDouble: {
            PVDouble *value = (PVDouble *)pv;
            value->put(from); return;
        }
        case pvString: {
            PVString *value = (PVString *)pv;
            char buffer[20];
            long int ival = from;
            sprintf(buffer,"%ld",ival);
            String xxx(buffer);
            value->put(xxx);
            return;
        }
    }
    throw std::logic_error(logicError);
}

void  Convert::fromFloat(PVScalar* pv, float from)
{
    ScalarConstPtr scalar = pv->getScalar();
    ScalarType scalarType = scalar->getScalarType();
    switch(scalarType) {
        case pvBoolean:
           throw std::logic_error(String("float can not be converted to boolean"));
        case pvByte: {
            PVByte *value = (PVByte *)pv;
            value->put(from); return;
        }
        case pvShort: {
            PVShort *value = (PVShort *)pv;
            value->put(from); return;
        }
        case pvInt: {
            PVInt *value = (PVInt *)pv;
            value->put(from); return;
        }
        case pvLong: {
            PVLong *value = (PVLong *)pv;
            value->put(from); return;
        }
        case pvFloat: {
            PVFloat *value = (PVFloat *)pv;
            value->put(from); return;
        }
        case pvDouble: {
            PVDouble *value = (PVDouble *)pv;
            value->put(from); return;
        }
        case pvString: {
            PVString *value = (PVString *)pv;
            char buffer[20];
            double dval = from;
            sprintf(buffer,"%e",dval);
            String xxx(buffer);
            value->put(xxx);
            return;
        }
    }
    throw std::logic_error(logicError);
}

void  Convert::fromDouble(PVScalar *pv, double from)
{
    ScalarConstPtr scalar = pv->getScalar();
    ScalarType scalarType = scalar->getScalarType();
    switch(scalarType) {
        case pvBoolean:
           throw std::logic_error(String("double can not be converted to boolean"));
        case pvByte: {
            PVByte *value = (PVByte *)pv;
            value->put(from); return;
        }
        case pvShort: {
            PVShort *value = (PVShort *)pv;
            value->put(from); return;
        }
        case pvInt: {
            PVInt *value = (PVInt *)pv;
            value->put(from); return;
        }
        case pvLong: {
            PVLong *value = (PVLong *)pv;
            value->put(from); return;
        }
        case pvFloat: {
            PVFloat *value = (PVFloat *)pv;
            value->put(from); return;
        }
        case pvDouble: {
            PVDouble *value = (PVDouble *)pv;
            value->put(from); return;
        }
        case pvString: {
            PVString *value = (PVString *)pv;
            char buffer[20];
            double dval = from;
            sprintf(buffer,"%e",dval);
            String xxx(buffer);
            value->put(xxx);
            return;
        }
    }
    throw std::logic_error(logicError);
}

int Convert::toByteArray(PVScalarArray * pv, int offset, int length,
    epicsInt8 to[], int toOffset)
{
    return convertToByteArray(pv, offset, length, to, toOffset);
}

int Convert::toShortArray(PVScalarArray * pv, int offset, int length,
    epicsInt16 to[], int toOffset)
{
    return convertToShortArray(pv, offset, length, to, toOffset);
}

int Convert::toIntArray(PVScalarArray * pv, int offset, int length,
    epicsInt32 to[], int toOffset)
{
    return convertToIntArray(pv, offset, length, to, toOffset);
}

int Convert::toLongArray(PVScalarArray * pv, int offset, int length,
    epicsInt64 to[], int toOffset)
{
    return convertToLongArray(pv, offset, length, to, toOffset);
}

int Convert::toFloatArray(PVScalarArray * pv, int offset, int length,
    float to[], int toOffset)
{
    return convertToFloatArray(pv, offset, length, to, toOffset);
}

int Convert::toDoubleArray(PVScalarArray * pv, int offset, int length,
    double to[], int toOffset)
{
    return convertToDoubleArray(pv, offset, length, to, toOffset);
}

int Convert::fromByteArray(PVScalarArray *pv, int offset, int length,
    epicsInt8 from[], int fromOffset)
{
    return convertFromByteArray(pv, offset, length, from, fromOffset);
}

int Convert::fromShortArray(PVScalarArray *pv, int offset, int length,
    epicsInt16 from[], int fromOffset)
{
    return convertFromShortArray(pv, offset, length, from, fromOffset);
}

int Convert::fromIntArray(PVScalarArray *pv, int offset, int length,
    epicsInt32 from[], int fromOffset)
{
    return convertFromIntArray(pv, offset, length, from, fromOffset);
}

int Convert::fromLongArray(PVScalarArray *pv, int offset, int length,
    epicsInt64 from[], int fromOffset)
{
    return convertFromLongArray(pv, offset, length, from, fromOffset);
}

int Convert::fromFloatArray(PVScalarArray *pv, int offset, int length,
    float from[], int fromOffset)
{
    return convertFromFloatArray(pv, offset, length, from, fromOffset);
}

int Convert::fromDoubleArray(PVScalarArray *pv, int offset, int length,
    double from[], int fromOffset)
{
    return convertFromDoubleArray(pv, offset, length, from, fromOffset);
}

void Convert::newLine(StringBuilder buffer, int indentLevel)
{
    *buffer += "\n";
    for(int i=0; i<indentLevel; i++) *buffer += "    ";
}

static bool scalarEquals(PVScalar *a,PVScalar *b)
{
    ScalarType ascalarType = a->getScalar()->getScalarType();
    ScalarType bscalarType = b->getScalar()->getScalarType();
    if(ascalarType!=bscalarType) return epicsFalse;
    switch(ascalarType) {
        case pvBoolean: {
            PVBoolean *pa = (PVBoolean *)a;
            PVBoolean *pb = (PVBoolean *)b;
            bool avalue = pa->get();
            bool bvalue = pb->get();
            return ((avalue==bvalue) ? epicsTrue : epicsFalse);
        }
        case pvByte: {
            PVByte *pa = (PVByte *)a;
            PVByte *pb = (PVByte *)b;
            epicsInt8 avalue = pa->get();
            epicsInt8 bvalue = pb->get();
            return ((avalue==bvalue) ? epicsTrue : epicsFalse);
        }
        case pvShort: {
            PVShort *pa = (PVShort *)a;
            PVShort *pb = (PVShort *)b;
            epicsInt16 avalue = pa->get();
            epicsInt16 bvalue = pb->get();
            return ((avalue==bvalue) ? epicsTrue : epicsFalse);
        }
        case pvInt: {
            PVInt *pa = (PVInt *)a;
            PVInt *pb = (PVInt *)b;
            epicsInt32 avalue = pa->get();
            epicsInt32 bvalue = pb->get();
            return ((avalue==bvalue) ? epicsTrue : epicsFalse);
        }
        case pvLong: {
            PVLong *pa = (PVLong *)a;
            PVLong *pb = (PVLong *)b;
            epicsInt64 avalue = pa->get();
            epicsInt64 bvalue = pb->get();
            return ((avalue==bvalue) ? epicsTrue : epicsFalse);
        }
        case pvFloat: {
            PVFloat *pa = (PVFloat *)a;
            PVFloat *pb = (PVFloat *)b;
            float avalue = pa->get();
            float bvalue = pb->get();
            return ((avalue==bvalue) ? epicsTrue : epicsFalse);
        }
        case pvDouble: {
            PVDouble *pa = (PVDouble *)a;
            PVDouble *pb = (PVDouble *)b;
            double avalue = pa->get();
            double bvalue = pb->get();
            return ((avalue==bvalue) ? epicsTrue : epicsFalse);
        }
        case pvString: {
            PVString *pa = (PVString *)a;
            PVString *pb = (PVString *)b;
            String avalue = pa->get();
            String bvalue = pb->get();
            return ((avalue==bvalue) ? epicsTrue : epicsFalse);
        }
    }
    String message("should not get here");
    throw std::logic_error(message);
}

static bool arrayEquals(PVScalarArray *a,PVScalarArray *b)
{
    if(a==b) return epicsTrue;
    ScalarType aType = a->getScalarArray()->getElementType();
    ScalarType bType = b->getScalarArray()->getElementType();
    if(aType!=bType) return epicsFalse;
    if(a->getLength()!=b->getLength()) return epicsFalse;
    int length = a->getLength();
    switch(aType) {
        case pvBoolean: {
            PVBooleanArray *aarray = (PVBooleanArray *)a;
            PVBooleanArray *barray = (PVBooleanArray *)b;
            BooleanArrayData adata = BooleanArrayData();
            BooleanArrayData bdata = BooleanArrayData();
            aarray->get(0,length,&adata);
            barray->get(0,length,&bdata);
            bool *avalue = adata.data;
            bool *bvalue = bdata.data;
            for(int i=0; i<length; i++) {
                if(avalue[i]!=bvalue[i]) return epicsFalse;
            }
            return epicsTrue;
        }
    }
    String message("should not get here");
    throw std::logic_error(message);
}

static bool structureArrayEquals(PVStructureArray *a,PVStructureArray *b)
{
    throw std::logic_error(notImplemented);
}

static bool structureEquals(PVStructure *a,PVStructure *b)
{
    throw std::logic_error(notImplemented);
}

bool convertEquals(PVField *a,PVField *b)
{
    if(a==b) return epicsTrue;
    Type atype = a->getField()->getType();
    Type btype = b->getField()->getType();
    if(atype!=btype) return epicsFalse;
    if(atype==scalar) return scalarEquals((PVScalar *)a,(PVScalar *)b);
    if(atype==scalarArray) {
         return arrayEquals((PVScalarArray *)a,(PVScalarArray *)b);
    }
    if(atype==structureArray) {
         return structureArrayEquals(
             (PVStructureArray *)a,(PVStructureArray *)b);
    }
    if(atype==structure) {
        return structureEquals((PVStructure *)a,(PVStructure *)b);
    }
    String message("should not get here");
    throw std::logic_error(message);
}

int convertFromByteArray(PVScalarArray *pv, int offset, int len,epicsInt8 from[], int fromOffset)
{
    throw std::logic_error(notImplemented);
}

int convertToByteArray(PVScalarArray * pv, int offset, int len,epicsInt8 to[], int toOffset)
{
    throw std::logic_error(notImplemented);
}

int convertFromShortArray(PVScalarArray *pv, int offset, int len,epicsInt16 from[], int fromOffset)
{
    throw std::logic_error(notImplemented);
}

int convertToShortArray(PVScalarArray * pv, int offset, int len,epicsInt16 to[], int toOffset)
{
    throw std::logic_error(notImplemented);
}

int convertFromIntArray(PVScalarArray *pv, int offset, int len,epicsInt32 from[], int fromOffset)
{
    throw std::logic_error(notImplemented);
}

int convertToIntArray(PVScalarArray * pv, int offset, int len,epicsInt32 to[], int toOffset)
{
    throw std::logic_error(notImplemented);
}

int convertFromLongArray(PVScalarArray *pv, int offset, int len,epicsInt64 from[], int fromOffset)
{
    throw std::logic_error(notImplemented);
}

int convertToLongArray(PVScalarArray * pv, int offset, int len,epicsInt64 to[], int toOffset)
{
    throw std::logic_error(notImplemented);
}

int convertFromFloatArray(PVScalarArray *pv, int offset, int len,float from[], int fromOffset)
{
    throw std::logic_error(notImplemented);
}

int convertToFloatArray(PVScalarArray * pv, int offset, int len,float to[], int toOffset)
{
    throw std::logic_error(notImplemented);
}

int convertFromDoubleArray(PVScalarArray *pv, int offset, int len,double from[], int fromOffset)
{
    throw std::logic_error(notImplemented);
}

int convertToDoubleArray(PVScalarArray * pv, int offset, int len,double to[], int toOffset)
{
    throw std::logic_error(notImplemented);
}

int convertFromStringArray(PVScalarArray *pv, int offset, int len,String from[], int fromOffset)
{
    throw std::logic_error(notImplemented);
}

int convertToStringArray(PVScalarArray * pv, int offset, int len,String to[], int toOffset)
{
    throw std::logic_error(notImplemented);
}


void convertToString(StringBuilder buffer,PVField * pv,int indentLevel)
{
    Type type = pv->getField()->getType();
    if(type==scalarArray) {
        return convertArray(buffer,(PVScalarArray *)pv,indentLevel);
    }
    if(type==structure) {
        return convertStructure(buffer,(PVStructure*)pv,indentLevel);
    }
    if(type==structureArray) {
    	return convertStructureArray(buffer,(PVStructureArray*)pv,indentLevel);
    }
    PVScalar *pvScalar = (PVScalar*)pv;
    switch(pvScalar->getScalar()->getScalarType()) {
    case pvBoolean: {
            PVBoolean *data = (PVBoolean*)pv;
            bool value = data->get();
            if(value) {
                *buffer += "true";
            } else {
                *buffer += "false";
            }
        }
        return;
    case pvByte: {
            PVByte *data = (PVByte*)pv;
            char xxx[30];
            sprintf(xxx,"%d",(int)data->get());
            *buffer += xxx;
        }
        return;
    case pvShort: {
            PVShort *data = (PVShort*)pv;
            char xxx[30];
            sprintf(xxx,"%d",(int)data->get());
            *buffer += xxx;
        }
        return;
    case pvInt: {
            PVInt *data = (PVInt*)pv;
            char xxx[30];
            sprintf(xxx,"%d",(int)data->get());
            *buffer += xxx;
        }
        return;
    case pvLong: {
            PVLong *data = (PVLong*)pv;
            char xxx[30];
            sprintf(xxx,"%ld",(long int)data->get());
            *buffer += xxx;
        }
        return;
    case pvFloat: {
            PVFloat *data = (PVFloat*)pv;
            char xxx[30];
            sprintf(xxx,"%g",data->get());
            *buffer += xxx;
        }
        return;
    case pvDouble: {
            PVDouble *data = (PVDouble*)pv;
            char xxx[30];
            sprintf(xxx,"%lg",data->get());
            *buffer += xxx;
        }
        return;
    case pvString: {
            PVString *data = (PVString*)pv;
            *buffer += data->get();
        }
        return;
    default:
        *buffer += "unknown ScalarType";
    }
}

void convertStructure(StringBuilder buffer,PVStructure *data,int indentLevel)
{
    throw std::logic_error(notImplemented);
}

void convertArray(StringBuilder buffer,PVScalarArray * pv,int indentLevel)
{
    ScalarArrayConstPtr array = pv->getScalarArray();
    ScalarType type = array->getElementType();
    switch(type) {
    case pvBoolean: {
            PVBooleanArray *pvdata = (PVBooleanArray*)pv;
            BooleanArrayData data = BooleanArrayData();
            *buffer += "[";
            for(int i=0; i < pvdata->getLength(); i++) {
                if(i!=0) *buffer += ",";
                int num = pvdata->get(i,1,&data);
                if(num==1) {
                     bool * value = data.data;
                     if(value[data.offset]) {
                         *buffer += "true";
                     } else {
                         *buffer += "false";
                     }
                } else {
                     *buffer += "???? ";
                }
            }
            *buffer += "]";
            break;
        }
    case pvByte: {
            PVByteArray *pvdata = (PVByteArray*)pv;
            ByteArrayData data = ByteArrayData();
            *buffer += "[";
            for(int i=0; i < pvdata->getLength(); i++) {
                if(i!=0) *buffer += ",";
                int num = pvdata->get(i,1,&data);
                if(num==1) {
                     int val = data.data[data.offset];
                     char buf[16];
                     sprintf(buf,"%d",val);
                     *buffer += buf;
                } else {
                     *buffer += "???? ";
                }
            }
            *buffer += "]";
            break;
        }
    case pvShort: {
            PVShortArray *pvdata = (PVShortArray*)pv;
            ShortArrayData data = ShortArrayData();
            *buffer += "[";
            for(int i=0; i < pvdata->getLength(); i++) {
                if(i!=0) *buffer += ',';
                int num = pvdata->get(i,1,&data);
                if(num==1) {
                     int val = data.data[data.offset];
                     char buf[16];
                     sprintf(buf,"%d",val);
                     *buffer += buf;
                } else {
                     *buffer += "???? ";
                }
            }
            *buffer += "]";
            break;
        }
    case pvInt: {
            PVIntArray *pvdata = (PVIntArray*)pv;
            IntArrayData data = IntArrayData();
            *buffer += "[";
            for(int i=0; i < pvdata->getLength(); i++) {
                if(i!=0) *buffer += ',';
                int num = pvdata->get(i,1,&data);
                if(num==1) {
                     int val = data.data[data.offset];
                     char buf[16];
                     sprintf(buf,"%d",val);
                     *buffer += buf;
                } else {
                     *buffer += "???? ";
                }
            }
            *buffer += "]";
            break;
        }
    case pvLong: {
            PVLongArray *pvdata = (PVLongArray*)pv;
            LongArrayData data = LongArrayData();
            *buffer += "[";
            for(int i=0; i < pvdata->getLength(); i++) {
                if(i!=0) *buffer += ',';
                int num = pvdata->get(i,1,&data);
                if(num==1) {
                     long int val = data.data[data.offset];
                     char buf[16];
                     sprintf(buf,"%ld",val);
                     *buffer += buf;
                } else {
                     *buffer += "???? ";
                }
            }
            *buffer += "]";
            break;
        }
    case pvFloat: {
            PVFloatArray *pvdata = (PVFloatArray*)pv;
            FloatArrayData data = FloatArrayData();
            *buffer += "[";
            for(int i=0; i < pvdata->getLength(); i++) {
                if(i!=0) *buffer += ',';
                int num = pvdata->get(i,1,&data);
                if(num==1) {
                     float val = data.data[data.offset];
                     char buf[16];
                     sprintf(buf,"%f",val);
                     *buffer += buf;
                } else {
                     *buffer += "???? ";
                }
            }
            *buffer += "]";
            break;
        }
    case pvDouble: {
            PVDoubleArray *pvdata = (PVDoubleArray*)pv;
            DoubleArrayData data = DoubleArrayData();
            *buffer += "[";
            for(int i=0; i < pvdata->getLength(); i++) {
                if(i!=0) *buffer += ',';
                int num = pvdata->get(i,1,&data);
                if(num==1) {
                     double val = data.data[data.offset];
                     char buf[16];
                     sprintf(buf,"%lf",val);
                     *buffer += buf;
                } else {
                     *buffer += "???? ";
                }
            }
            *buffer += ("]");
            break;
        }
    case pvString: {
    	PVStringArray *pvdata = (PVStringArray*)pv;
    	StringArrayData data = StringArrayData();
    	*buffer += "[";
    	for(int i=0; i < pvdata->getLength(); i++) {
    		if(i!=0) *buffer += ",";
    		int num = pvdata->get(i,1,&data);
    		StringPtrArray value = data.data;
                if(num==1) {
                    if(value[data.offset].length()>0) {
                         *buffer += value[data.offset].c_str();
                    } else {
                         *buffer += "null";
                    }
    		} else {
    			*buffer += "null";
    		}
    	}
    	*buffer += "]";
    	break;
    }
    default:
        *buffer += " array element is unknown ScalarType";
    }
    if(pv->isImmutable()) {
        *buffer += " immutable ";
    }
}

void convertStructureArray(StringBuilder buffer,PVStructureArray * pvdata,int indentLevel)
{
    throw std::logic_error(notImplemented);
}

int copyArrayDataReference(PVScalarArray *from,PVArray *to)
{
    throw std::logic_error(notImplemented);
}

int CopyNumericArray(PVScalarArray *from, int offset, PVScalarArray *to, int toOffset, int len)
{
    throw std::logic_error(notImplemented);
}

class ConvertExt : public Convert {
public:
    ConvertExt(): Convert(){};
};

Convert * getConvert() {
    static Mutex mutex = Mutex();
    Lock xx(&mutex);

    if(convert==0){
        convert = new ConvertExt();
    }
    return convert;
}

}}