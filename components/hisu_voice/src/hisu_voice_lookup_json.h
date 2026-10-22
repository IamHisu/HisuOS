#pragma once
#include "cJSON.h"

/* Private cJSON 1.7.19 namespace. Lookup uses aliases; music uses narrow wrappers.
 * Keep declarations synchronized with the managed dependency; host tests verify it.
 * Never attach these trees to another module or change either instance's hooks. */
#define cJSON_Version hisu_lookup_cJSON_Version
#define cJSON_Duplicate_rec hisu_lookup_cJSON_Duplicate_rec
#define cJSON_InitHooks hisu_lookup_cJSON_InitHooks
#define cJSON_Parse hisu_lookup_cJSON_Parse
#define cJSON_ParseWithLength hisu_lookup_cJSON_ParseWithLength
#define cJSON_ParseWithOpts hisu_lookup_cJSON_ParseWithOpts
#define cJSON_ParseWithLengthOpts hisu_lookup_cJSON_ParseWithLengthOpts
#define cJSON_Print hisu_lookup_cJSON_Print
#define cJSON_PrintUnformatted hisu_lookup_cJSON_PrintUnformatted
#define cJSON_PrintBuffered hisu_lookup_cJSON_PrintBuffered
#define cJSON_PrintPreallocated hisu_lookup_cJSON_PrintPreallocated
#define cJSON_Delete hisu_lookup_cJSON_Delete
#define cJSON_GetArraySize hisu_lookup_cJSON_GetArraySize
#define cJSON_GetArrayItem hisu_lookup_cJSON_GetArrayItem
#define cJSON_GetObjectItem hisu_lookup_cJSON_GetObjectItem
#define cJSON_GetObjectItemCaseSensitive hisu_lookup_cJSON_GetObjectItemCaseSensitive
#define cJSON_HasObjectItem hisu_lookup_cJSON_HasObjectItem
#define cJSON_GetErrorPtr hisu_lookup_cJSON_GetErrorPtr
#define cJSON_GetStringValue hisu_lookup_cJSON_GetStringValue
#define cJSON_GetNumberValue hisu_lookup_cJSON_GetNumberValue
#define cJSON_IsInvalid hisu_lookup_cJSON_IsInvalid
#define cJSON_IsFalse hisu_lookup_cJSON_IsFalse
#define cJSON_IsTrue hisu_lookup_cJSON_IsTrue
#define cJSON_IsBool hisu_lookup_cJSON_IsBool
#define cJSON_IsNull hisu_lookup_cJSON_IsNull
#define cJSON_IsNumber hisu_lookup_cJSON_IsNumber
#define cJSON_IsString hisu_lookup_cJSON_IsString
#define cJSON_IsArray hisu_lookup_cJSON_IsArray
#define cJSON_IsObject hisu_lookup_cJSON_IsObject
#define cJSON_IsRaw hisu_lookup_cJSON_IsRaw
#define cJSON_CreateNull hisu_lookup_cJSON_CreateNull
#define cJSON_CreateTrue hisu_lookup_cJSON_CreateTrue
#define cJSON_CreateFalse hisu_lookup_cJSON_CreateFalse
#define cJSON_CreateBool hisu_lookup_cJSON_CreateBool
#define cJSON_CreateNumber hisu_lookup_cJSON_CreateNumber
#define cJSON_CreateString hisu_lookup_cJSON_CreateString
#define cJSON_CreateRaw hisu_lookup_cJSON_CreateRaw
#define cJSON_CreateArray hisu_lookup_cJSON_CreateArray
#define cJSON_CreateObject hisu_lookup_cJSON_CreateObject
#define cJSON_CreateStringReference hisu_lookup_cJSON_CreateStringReference
#define cJSON_CreateObjectReference hisu_lookup_cJSON_CreateObjectReference
#define cJSON_CreateArrayReference hisu_lookup_cJSON_CreateArrayReference
#define cJSON_CreateIntArray hisu_lookup_cJSON_CreateIntArray
#define cJSON_CreateFloatArray hisu_lookup_cJSON_CreateFloatArray
#define cJSON_CreateDoubleArray hisu_lookup_cJSON_CreateDoubleArray
#define cJSON_CreateStringArray hisu_lookup_cJSON_CreateStringArray
#define cJSON_AddItemToArray hisu_lookup_cJSON_AddItemToArray
#define cJSON_AddItemToObject hisu_lookup_cJSON_AddItemToObject
#define cJSON_AddItemToObjectCS hisu_lookup_cJSON_AddItemToObjectCS
#define cJSON_AddItemReferenceToArray hisu_lookup_cJSON_AddItemReferenceToArray
#define cJSON_AddItemReferenceToObject hisu_lookup_cJSON_AddItemReferenceToObject
#define cJSON_DetachItemViaPointer hisu_lookup_cJSON_DetachItemViaPointer
#define cJSON_DetachItemFromArray hisu_lookup_cJSON_DetachItemFromArray
#define cJSON_DeleteItemFromArray hisu_lookup_cJSON_DeleteItemFromArray
#define cJSON_DetachItemFromObject hisu_lookup_cJSON_DetachItemFromObject
#define cJSON_DetachItemFromObjectCaseSensitive hisu_lookup_cJSON_DetachItemFromObjectCaseSensitive
#define cJSON_DeleteItemFromObject hisu_lookup_cJSON_DeleteItemFromObject
#define cJSON_DeleteItemFromObjectCaseSensitive hisu_lookup_cJSON_DeleteItemFromObjectCaseSensitive
#define cJSON_InsertItemInArray hisu_lookup_cJSON_InsertItemInArray
#define cJSON_ReplaceItemViaPointer hisu_lookup_cJSON_ReplaceItemViaPointer
#define cJSON_ReplaceItemInArray hisu_lookup_cJSON_ReplaceItemInArray
#define cJSON_ReplaceItemInObject hisu_lookup_cJSON_ReplaceItemInObject
#define cJSON_ReplaceItemInObjectCaseSensitive hisu_lookup_cJSON_ReplaceItemInObjectCaseSensitive
#define cJSON_Duplicate hisu_lookup_cJSON_Duplicate
#define cJSON_Compare hisu_lookup_cJSON_Compare
#define cJSON_Minify hisu_lookup_cJSON_Minify
#define cJSON_AddNullToObject hisu_lookup_cJSON_AddNullToObject
#define cJSON_AddTrueToObject hisu_lookup_cJSON_AddTrueToObject
#define cJSON_AddFalseToObject hisu_lookup_cJSON_AddFalseToObject
#define cJSON_AddBoolToObject hisu_lookup_cJSON_AddBoolToObject
#define cJSON_AddNumberToObject hisu_lookup_cJSON_AddNumberToObject
#define cJSON_AddStringToObject hisu_lookup_cJSON_AddStringToObject
#define cJSON_AddRawToObject hisu_lookup_cJSON_AddRawToObject
#define cJSON_AddObjectToObject hisu_lookup_cJSON_AddObjectToObject
#define cJSON_AddArrayToObject hisu_lookup_cJSON_AddArrayToObject
#define cJSON_SetNumberHelper hisu_lookup_cJSON_SetNumberHelper
#define cJSON_SetValuestring hisu_lookup_cJSON_SetValuestring
#define cJSON_malloc hisu_lookup_cJSON_malloc
#define cJSON_free hisu_lookup_cJSON_free

CJSON_PUBLIC(const char*) cJSON_Version(void);
CJSON_PUBLIC(void) cJSON_InitHooks(cJSON_Hooks* hooks);
CJSON_PUBLIC(cJSON *) cJSON_Parse(const char *value);
CJSON_PUBLIC(cJSON *) cJSON_ParseWithLength(const char *value, size_t buffer_length);
CJSON_PUBLIC(cJSON *) cJSON_ParseWithOpts(const char *value, const char **return_parse_end, cJSON_bool require_null_terminated);
CJSON_PUBLIC(cJSON *) cJSON_ParseWithLengthOpts(const char *value, size_t buffer_length, const char **return_parse_end, cJSON_bool require_null_terminated);
CJSON_PUBLIC(char *) cJSON_Print(const cJSON *item);
CJSON_PUBLIC(char *) cJSON_PrintUnformatted(const cJSON *item);
CJSON_PUBLIC(char *) cJSON_PrintBuffered(const cJSON *item, int prebuffer, cJSON_bool fmt);
CJSON_PUBLIC(cJSON_bool) cJSON_PrintPreallocated(cJSON *item, char *buffer, const int length, const cJSON_bool format);
CJSON_PUBLIC(void) cJSON_Delete(cJSON *item);
CJSON_PUBLIC(int) cJSON_GetArraySize(const cJSON *array);
CJSON_PUBLIC(cJSON *) cJSON_GetArrayItem(const cJSON *array, int index);
CJSON_PUBLIC(cJSON *) cJSON_GetObjectItem(const cJSON * const object, const char * const string);
CJSON_PUBLIC(cJSON *) cJSON_GetObjectItemCaseSensitive(const cJSON * const object, const char * const string);
CJSON_PUBLIC(cJSON_bool) cJSON_HasObjectItem(const cJSON *object, const char *string);
CJSON_PUBLIC(const char *) cJSON_GetErrorPtr(void);
CJSON_PUBLIC(char *) cJSON_GetStringValue(const cJSON * const item);
CJSON_PUBLIC(double) cJSON_GetNumberValue(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsInvalid(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsFalse(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsTrue(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsBool(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsNull(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsNumber(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsString(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsArray(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsObject(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsRaw(const cJSON * const item);
CJSON_PUBLIC(cJSON *) cJSON_CreateNull(void);
CJSON_PUBLIC(cJSON *) cJSON_CreateTrue(void);
CJSON_PUBLIC(cJSON *) cJSON_CreateFalse(void);
CJSON_PUBLIC(cJSON *) cJSON_CreateBool(cJSON_bool boolean);
CJSON_PUBLIC(cJSON *) cJSON_CreateNumber(double num);
CJSON_PUBLIC(cJSON *) cJSON_CreateString(const char *string);
CJSON_PUBLIC(cJSON *) cJSON_CreateRaw(const char *raw);
CJSON_PUBLIC(cJSON *) cJSON_CreateArray(void);
CJSON_PUBLIC(cJSON *) cJSON_CreateObject(void);
CJSON_PUBLIC(cJSON *) cJSON_CreateStringReference(const char *string);
CJSON_PUBLIC(cJSON *) cJSON_CreateObjectReference(const cJSON *child);
CJSON_PUBLIC(cJSON *) cJSON_CreateArrayReference(const cJSON *child);
CJSON_PUBLIC(cJSON *) cJSON_CreateIntArray(const int *numbers, int count);
CJSON_PUBLIC(cJSON *) cJSON_CreateFloatArray(const float *numbers, int count);
CJSON_PUBLIC(cJSON *) cJSON_CreateDoubleArray(const double *numbers, int count);
CJSON_PUBLIC(cJSON *) cJSON_CreateStringArray(const char *const *strings, int count);
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToArray(cJSON *array, cJSON *item);
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item);
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToObjectCS(cJSON *object, const char *string, cJSON *item);
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemReferenceToArray(cJSON *array, cJSON *item);
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemReferenceToObject(cJSON *object, const char *string, cJSON *item);
CJSON_PUBLIC(cJSON *) cJSON_DetachItemViaPointer(cJSON *parent, cJSON * const item);
CJSON_PUBLIC(cJSON *) cJSON_DetachItemFromArray(cJSON *array, int which);
CJSON_PUBLIC(void) cJSON_DeleteItemFromArray(cJSON *array, int which);
CJSON_PUBLIC(cJSON *) cJSON_DetachItemFromObject(cJSON *object, const char *string);
CJSON_PUBLIC(cJSON *) cJSON_DetachItemFromObjectCaseSensitive(cJSON *object, const char *string);
CJSON_PUBLIC(void) cJSON_DeleteItemFromObject(cJSON *object, const char *string);
CJSON_PUBLIC(void) cJSON_DeleteItemFromObjectCaseSensitive(cJSON *object, const char *string);
CJSON_PUBLIC(cJSON_bool) cJSON_InsertItemInArray(cJSON *array, int which, cJSON *newitem); /* Shifts pre-existing items to the right. */
CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemViaPointer(cJSON * const parent, cJSON * const item, cJSON * replacement);
CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemInArray(cJSON *array, int which, cJSON *newitem);
CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemInObject(cJSON *object,const char *string,cJSON *newitem);
CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemInObjectCaseSensitive(cJSON *object,const char *string,cJSON *newitem);
CJSON_PUBLIC(cJSON *) cJSON_Duplicate(const cJSON *item, cJSON_bool recurse);
CJSON_PUBLIC(cJSON_bool) cJSON_Compare(const cJSON * const a, const cJSON * const b, const cJSON_bool case_sensitive);
CJSON_PUBLIC(void) cJSON_Minify(char *json);
CJSON_PUBLIC(cJSON*) cJSON_AddNullToObject(cJSON * const object, const char * const name);
CJSON_PUBLIC(cJSON*) cJSON_AddTrueToObject(cJSON * const object, const char * const name);
CJSON_PUBLIC(cJSON*) cJSON_AddFalseToObject(cJSON * const object, const char * const name);
CJSON_PUBLIC(cJSON*) cJSON_AddBoolToObject(cJSON * const object, const char * const name, const cJSON_bool boolean);
CJSON_PUBLIC(cJSON*) cJSON_AddNumberToObject(cJSON * const object, const char * const name, const double number);
CJSON_PUBLIC(cJSON*) cJSON_AddStringToObject(cJSON * const object, const char * const name, const char * const string);
CJSON_PUBLIC(cJSON*) cJSON_AddRawToObject(cJSON * const object, const char * const name, const char * const raw);
CJSON_PUBLIC(cJSON*) cJSON_AddObjectToObject(cJSON * const object, const char * const name);
CJSON_PUBLIC(cJSON*) cJSON_AddArrayToObject(cJSON * const object, const char * const name);
CJSON_PUBLIC(double) cJSON_SetNumberHelper(cJSON *object, double number);
CJSON_PUBLIC(char*) cJSON_SetValuestring(cJSON *object, const char *valuestring);
CJSON_PUBLIC(void *) cJSON_malloc(size_t size);
CJSON_PUBLIC(void) cJSON_free(void *object);

