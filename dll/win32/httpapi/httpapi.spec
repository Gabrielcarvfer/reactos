@ stub HttpAddFragmentToCache
@ stdcall -stub HttpAddUrl(ptr wstr ptr)
@ stdcall HttpAddUrlToUrlGroup(int64 wstr int64 long)
@ stub HttpCreateAppPool
@ stub HttpCreateConfigGroup
@ stub HttpCreateFilter
@ stdcall -stub HttpCreateHttpHandle(ptr long)
@ stdcall -version=0x600+ HttpCreateRequestQueue(long wstr ptr long ptr)
@ stdcall -version=0x600+ HttpCreateServerSession(long ptr long)
@ stdcall -version=0x600+ HttpCreateUrlGroup(int64 ptr long)
@ stub HttpDeleteConfigGroup
@ stdcall -stub HttpDeleteServiceConfiguration(ptr long ptr long ptr)
@ stub HttpFilterAccept
@ stub HttpFilterAppRead
@ stub HttpFilterAppWrite
@ stub HttpFilterAppWriteAndRawRead
@ stub HttpFilterClose
@ stub HttpFilterRawRead
@ stub HttpFilterRawWrite
@ stub HttpFilterRawWriteAndAppRead
@ stub HttpFlushResponseCache
@ stub HttpGetCounters
@ stdcall HttpInitialize(long long ptr)
@ stub HttpOpenAppPool
@ stub HttpOpenControlChannel
@ stub HttpOpenFilter
@ stub HttpQueryAppPoolInformation
@ stub HttpQueryConfigGroupInformation
@ stub HttpQueryControlChannelInformation
@ stdcall -stub HttpQueryServiceConfiguration(ptr long ptr long ptr long ptr ptr)
@ stub HttpReadFragmentFromCache
@ stub HttpReceiveClientCertificate
@ stdcall HttpReceiveHttpRequest(ptr int64 long ptr long ptr ptr)
@ stdcall -stub HttpReceiveRequestEntityBody(ptr int64 long ptr long ptr ptr)
@ stub HttpRemoveAllUrlsFromConfigGroup
@ stdcall -stub HttpRemoveUrl(ptr wstr)
@ stdcall HttpRemoveUrlFromUrlGroup(int64 wstr long)
@ stdcall HttpSendHttpResponse(ptr int64 long ptr ptr ptr ptr long ptr ptr)
@ stub HttpSendResponseEntityBody
@ stub HttpSetAppPoolInformation
@ stub HttpSetConfigGroupInformation
@ stub HttpSetControlChannelInformation
@ stdcall -stub HttpSetServiceConfiguration(ptr long ptr long ptr)
@ stdcall -version=0x600+ HttpSetUrlGroupProperty(int64 long ptr long)
@ stub HttpShutdownAppPool
@ stub HttpShutdownFilter
@ stdcall -version=0x600+ HttpShutdownRequestQueue(ptr)
@ stdcall HttpTerminate(long ptr)
@ stub HttpWaitForDemandStart
@ stub HttpWaitForDisconnect
