#include <pybind11/pybind11.h>
#include <pthread.h>

extern "C" {
#include "blast.h"
}

namespace py = pybind11;
using namespace pybind11::literals;

class dclimplode_decompressobj{
	std::string instr;
	std::vector<unsigned char>outstr;
	int requireInput;
	int hasInput;
	int first;
	pthread_t thread;
public:
	int finished;
    dclimplode_decompressobj(): requireInput(0), hasInput(0), first(1), finished(0), thread(NULL){
    }
	~dclimplode_decompressobj(){
		pthread_cancel(thread);
	}

	int put(unsigned char *buf, unsigned int len){
		outstr.reserve(outstr.capacity() + len);
		outstr.insert(outstr.end(), buf, buf+len);
		return 0;
	}
	unsigned int get(unsigned char **buf){
		requireInput = 1;
		for(;!hasInput;)usleep(1000);
		requireInput = 0;
		hasInput = 0;
		*buf = (unsigned char*)instr.data();
		return instr.size();
	}
	static int C_put(void *out_desc, unsigned char *buf, unsigned int len){
		return ((dclimplode_decompressobj*)out_desc)->put(buf, len);
	}
	static unsigned int C_get(void *in_desc, unsigned char **buf){
		return ((dclimplode_decompressobj*)in_desc)->get(buf);
	}
	static void* C_impl(void *ptr){
		int x = blast(C_get, ptr, C_put, ptr, NULL, NULL);
		((dclimplode_decompressobj*)ptr)->finished = 1;
		return NULL;
	}

    py::bytes decompress(const py::bytes &obj){
		outstr.resize(0);
        {
            char *buffer = nullptr;
            ssize_t length = 0;
            PYBIND11_BYTES_AS_STRING_AND_SIZE(obj.ptr(), &buffer, &length);
			instr = std::string(buffer, length);
			hasInput = 1;
        }
		if(first)pthread_create(&thread,NULL,C_impl,this);
		first=0;
		for(;hasInput;)usleep(1000);
		for(;!requireInput && !finished;)usleep(1000);
		if(finished)pthread_join(thread,NULL);
		return py::bytes((char*)outstr.data(), outstr.size());
    }
};

PYBIND11_MODULE(dclimplode, m){
    py::class_<dclimplode_decompressobj, std::shared_ptr<dclimplode_decompressobj> >(m, "decompressobj")
    .def(py::init<>())
    .def("decompress", &dclimplode_decompressobj::decompress,
     "obj"_a
    )
	.def_readonly("eof", &dclimplode_decompressobj::finished)
    ;

    //m.attr("SLZ_FMT_GZIP") = int(SLZ_FMT_GZIP);
    //m.attr("SLZ_FMT_ZLIB") = int(SLZ_FMT_ZLIB);
    //m.attr("SLZ_FMT_DEFLATE") = int(SLZ_FMT_DEFLATE);
}
