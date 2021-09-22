#include <pybind11/pybind11.h>
#include <pthread.h>

extern "C" {
#include "blast/blast.h"
#include "pklib/pklib.h"
}

const unsigned int SLEEP_US = 10;

namespace py = pybind11;
using namespace pybind11::literals;

template <typename ... Args>
std::string format(const std::string& fmt, Args ... args ){
    // http://pyopyopyo.hatenablog.com/entry/2019/02/08/102456
    size_t len = std::snprintf( nullptr, 0, fmt.c_str(), args ... );
    std::vector<char> buf(len + 1);
    std::snprintf(&buf[0], len + 1, fmt.c_str(), args ... );
    return std::string(&buf[0], &buf[0] + len);
}

class dclimplode_compressobj{
    std::string instr;
    std::vector<char>outstr;
    bool requireInput;
    bool hasInput;
    bool first;
    int result;
    pthread_t thread;

    unsigned int offset;
    unsigned int typ;
    unsigned int dsize;
    TCmpStruct CmpStruct;
public:
    bool finished;
    dclimplode_compressobj(int typ=0, int dsize=4096):
        requireInput(false), hasInput(false), first(true), finished(false), result(0), thread(NULL),
        typ(typ), dsize(dsize)
    {
        if(typ>1)throw std::invalid_argument(format("invalid type, must be 0 or 1 (%d)", typ));
        if(dsize!=1024 && dsize!=2048 && dsize!=4096)throw std::invalid_argument(format("invalid dsize, must be 1024, 2048 or 4096 (%d)", dsize));
        outstr.reserve(65536);
    }
    ~dclimplode_compressobj(){
        pthread_cancel(thread);
    }

    void put(char *buf, unsigned int len){
        outstr.insert(outstr.end(), buf, buf+len);
    }
    unsigned int get(char *buf, unsigned int size){
        if(offset == instr.size()){
            requireInput = true;
            for(;!hasInput;)usleep(SLEEP_US);
            requireInput = false;
            offset = 0;
        }
        hasInput = false;
        unsigned int copysize = offset+size > instr.size() ? instr.size()-offset : size;
        memcpy(buf,instr.data()+offset,copysize);
        offset+=copysize;
        return copysize;
    }
    static void C_put(char *buf, unsigned int *size, void *param){
        ((dclimplode_compressobj*)param)->put(buf, *size);
    }
    static unsigned int C_get(char *buf, unsigned int *size, void *param){
        return ((dclimplode_compressobj*)param)->get(buf, *size);
    }
    static void* C_impl(void *ptr){
        TCmpStruct *pCmpStruct = &((dclimplode_compressobj*)ptr)->CmpStruct;
        memset(pCmpStruct, 0, sizeof(TCmpStruct));
        ((dclimplode_compressobj*)ptr)->result = implode(C_get, C_put, (char*)pCmpStruct, ptr, &((dclimplode_compressobj*)ptr)->typ, &((dclimplode_compressobj*)ptr)->dsize);
        ((dclimplode_compressobj*)ptr)->finished = true;
        return NULL;
    }

    py::bytes compress(const py::bytes &obj){
        outstr.resize(0);
        {
            char *buffer = nullptr;
            ssize_t length = 0;
            PYBIND11_BYTES_AS_STRING_AND_SIZE(obj.ptr(), &buffer, &length);
            instr = std::string(buffer, length);
            hasInput = true;
        }
        if(first)offset=0,pthread_create(&thread,NULL,C_impl,this);
        first=false;
        for(;hasInput && !finished;)usleep(SLEEP_US);
        for(;!requireInput && !finished;)usleep(SLEEP_US);
        if(finished){
            pthread_join(thread,NULL);
            if(result)throw std::runtime_error(format("implode() error (%d)", result));
        }
        return py::bytes((char*)outstr.data(), outstr.size());
    }

    py::bytes flush(){
        outstr.resize(0);
        {
            instr = "";
            hasInput = true;
        }
        if(first)offset=0,pthread_create(&thread,NULL,C_impl,this);
        first=false;
        for(;hasInput && !finished;)usleep(SLEEP_US);
        for(;!requireInput && !finished;)usleep(SLEEP_US);
        if(finished)pthread_join(thread,NULL);
        return py::bytes((char*)outstr.data(), outstr.size());
    }
};

class dclimplode_decompressobj{
    std::string instr;
    std::vector<unsigned char>outstr;
    bool requireInput;
    bool hasInput;
    bool first;
    int result;
    pthread_t thread;
public:
    bool finished;
    dclimplode_decompressobj(): requireInput(false), hasInput(false), first(true), finished(false), result(0), thread(NULL){
        outstr.reserve(65536);
    }
    ~dclimplode_decompressobj(){
        pthread_cancel(thread);
    }

    int put(unsigned char *buf, unsigned int len){
        outstr.insert(outstr.end(), buf, buf+len);
        return 0;
    }
    unsigned int get(unsigned char **buf){
        requireInput = true;
        for(;!hasInput;)usleep(SLEEP_US);
        requireInput = false;
        hasInput = false;
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
        ((dclimplode_decompressobj*)ptr)->result = blast(C_get, ptr, C_put, ptr, NULL, NULL);
        ((dclimplode_decompressobj*)ptr)->finished = true;
        return NULL;
    }

    py::bytes decompress(const py::bytes &obj){
        outstr.resize(0);
        {
            char *buffer = nullptr;
            ssize_t length = 0;
            PYBIND11_BYTES_AS_STRING_AND_SIZE(obj.ptr(), &buffer, &length);
            instr = std::string(buffer, length);
            hasInput = true;
        }
        if(first)pthread_create(&thread,NULL,C_impl,this);
        first=false;
        for(;hasInput && !finished;)usleep(SLEEP_US);
        for(;!requireInput && !finished;)usleep(SLEEP_US);
        if(finished){
            pthread_join(thread,NULL);
            if(result)throw std::runtime_error(format("blast() error (%d)", result));
        }
        return py::bytes((char*)outstr.data(), outstr.size());
    }
};

PYBIND11_MODULE(dclimplode, m){
    py::class_<dclimplode_compressobj, std::shared_ptr<dclimplode_compressobj> >(m, "compressobj")
    .def(py::init<int, int>(), "type"_a=1, "dictsize"_a=4096)
    .def("compress", &dclimplode_compressobj::compress,
     "obj"_a
    )
    .def("flush", &dclimplode_compressobj::flush)
    ;

    py::class_<dclimplode_decompressobj, std::shared_ptr<dclimplode_decompressobj> >(m, "decompressobj")
    .def(py::init<>())
    .def("decompress", &dclimplode_decompressobj::decompress,
     "obj"_a
    )
    .def_readonly("eof", &dclimplode_decompressobj::finished)
    ;

    m.attr("CMP_BINARY") = int(CMP_BINARY);
    m.attr("CMP_ASCII") = int(CMP_ASCII);
}
