#include <Common/Log.h>
#include <D3dDdi/Log/AdapterCallbacksLog.h>

std::ostream& operator<<(std::ostream& os, const D3DDDICB_QUERYADAPTERINFO& data)
{
	return Compat::LogStruct(os)
		<< data.pPrivateDriverData
		<< data.PrivateDriverDataSize;
}

std::ostream& operator<<(std::ostream& os, const D3DDDICB_QUERYADAPTERINFO2& data)
{
	return Compat::LogStruct(os)
		<< data.QueryType
		<< data.pPrivateDriverData
		<< data.PrivateDriverDataSize;
}
