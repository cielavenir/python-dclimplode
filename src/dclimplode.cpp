#if defined(_WIN32) || (!defined(__GNUC__) && !defined(__clang__))
#define _hypot hypot
#include <cmath>
#endif

#include <pybind11/pybind11.h>

#include <algorithm>
#include <atomic>
#include <limits>
#include <mutex>

extern "C" {
#include "blast/blast.h"
#include "pklib/pklib.h"
}

const unsigned int SLEEP_US = 10;

#include <chrono>
#include <thread>
// sleep_for is not a Windows pthread cancellation point; worker loops check explicitly.
#define usleep(usec) std::this_thread::sleep_for(std::chrono::microseconds(usec))

#if defined(_WIN32) || (!defined(__GNUC__) && !defined(__clang__))
#include "winpthreads.h"
#define ssize_t Py_ssize_t
#else
#include <pthread.h>
#endif

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
    std::atomic<bool> requireInput;
    std::atomic<bool> hasInput;
    std::atomic<bool> finished;
    bool threadActive;
    int result;
    pthread_t thread;
    std::mutex apiMutex;

    size_t offset;
    unsigned int typ;
    unsigned int dsize;
    TCmpStruct CmpStruct;
public:
    dclimplode_compressobj(int typ=0, int dsize=4096):
        requireInput(false), hasInput(false), finished(false), threadActive(false), result(0), thread(),
        offset(0), typ(typ), dsize(dsize)
    {
        if(typ<0 || typ>1)throw std::invalid_argument(format("invalid type, must be 0 or 1 (%d)", typ));
        if(dsize!=1024 && dsize!=2048 && dsize!=4096)throw std::invalid_argument(format("invalid dsize, must be 1024, 2048 or 4096 (%d)", dsize));
        outstr.reserve(65536);
    }
    // Join cancelled workers before destroying the buffers they access.
    ~dclimplode_compressobj(){
        if(threadActive){
            pthread_cancel(thread);
            pthread_join(thread,NULL);
        }
    }

    void put(char *buf, unsigned int len){
        outstr.insert(outstr.end(), buf, buf+len);
    }
    unsigned int get(char *buf, unsigned int size){
        if(offset == instr.size()){
            requireInput.store(true);
            for(;!hasInput.load();){pthread_testcancel();usleep(SLEEP_US);}
            requireInput.store(false);
            offset = 0;
        }
        hasInput.store(false);
        size_t copysize = (std::min)(static_cast<size_t>(size), instr.size()-offset);
        memcpy(buf,instr.data()+offset,copysize);
        offset+=copysize;
        return static_cast<unsigned int>(copysize);
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
        ((dclimplode_compressobj*)ptr)->finished.store(true);
        return NULL;
    }

    void start_worker(){
        if(threadActive)return;
        int error = pthread_create(&thread,NULL,C_impl,this);
        if(error)throw std::runtime_error(format("pthread_create() error (%d)", error));
        threadActive = true;
    }

    void join_worker(){
        if(!threadActive)return;
        int error = pthread_join(thread,NULL);
        if(error)throw std::runtime_error(format("pthread_join() error (%d)", error));
        threadActive = false;
        thread = pthread_t();
    }

    py::bytes compress(const py::bytes &obj){
        std::unique_lock<std::mutex> lock(apiMutex, std::try_to_lock);
        if(!lock.owns_lock())throw std::runtime_error("concurrent compressor calls are not supported");
        if(finished.load())throw std::runtime_error("compressor is already finalized");
        outstr.resize(0);
        {
            char *buffer = nullptr;
            ssize_t length = 0;
            PYBIND11_BYTES_AS_STRING_AND_SIZE(obj.ptr(), &buffer, &length);
            if(length == 0)return py::bytes();
            instr = std::string(buffer, length);
            hasInput.store(true);
        }
        {
            py::gil_scoped_release release;
            start_worker();
            for(;hasInput.load() && !finished.load();)usleep(SLEEP_US);
            for(;!requireInput.load() && !finished.load();)usleep(SLEEP_US);
            if(finished.load()){
                join_worker();
                if(result)throw std::runtime_error(format("implode() error (%d)", result));
            }
        }
        return py::bytes((char*)outstr.data(), outstr.size());
    }

    py::bytes flush(){
        std::unique_lock<std::mutex> lock(apiMutex, std::try_to_lock);
        if(!lock.owns_lock())throw std::runtime_error("concurrent compressor calls are not supported");
        if(finished.load())throw std::runtime_error("compressor is already finalized");
        outstr.resize(0);
        {
            instr = "";
            hasInput.store(true);
        }
        {
            py::gil_scoped_release release;
            start_worker();
            for(;hasInput.load() && !finished.load();)usleep(SLEEP_US);
            for(;!requireInput.load() && !finished.load();)usleep(SLEEP_US);
            if(finished.load()){
                join_worker();
                if(result)throw std::runtime_error(format("implode() error (%d)", result));
            }
        }
        return py::bytes((char*)outstr.data(), outstr.size());
    }
};

class dclimplode_decompressobj_blast{
    std::string instr;
    std::vector<unsigned char>outstr;
    std::atomic<bool> requireInput;
    std::atomic<bool> hasInput;
    std::atomic<bool> finished;
    bool threadActive;
    int result;
    pthread_t thread;
    std::mutex apiMutex;
    size_t offset;
public:
    dclimplode_decompressobj_blast(): requireInput(false), hasInput(false), finished(false), threadActive(false), result(0), thread(), offset(0){
        outstr.reserve(65536);
    }
    ~dclimplode_decompressobj_blast(){
        if(threadActive){
            pthread_cancel(thread);
            pthread_join(thread,NULL);
        }
    }

    int put(unsigned char *buf, unsigned int len){
        outstr.insert(outstr.end(), buf, buf+len);
        return 0;
    }
    unsigned int get(unsigned char **buf){
        if(offset == instr.size()){
            requireInput.store(true);
            for(;!hasInput.load();){pthread_testcancel();usleep(SLEEP_US);}
            requireInput.store(false);
            offset = 0;
        }
        hasInput.store(false);
        size_t copysize = (std::min)(instr.size()-offset, static_cast<size_t>((std::numeric_limits<unsigned int>::max)()));
        *buf = (unsigned char*)instr.data()+offset;
        offset += copysize;
        return static_cast<unsigned int>(copysize);
    }
    static int C_put(void *out_desc, unsigned char *buf, unsigned int len){
        return ((dclimplode_decompressobj_blast*)out_desc)->put(buf, len);
    }
    static unsigned int C_get(void *in_desc, unsigned char **buf){
        return ((dclimplode_decompressobj_blast*)in_desc)->get(buf);
    }
    static void* C_impl(void *ptr){
        ((dclimplode_decompressobj_blast*)ptr)->result = blast(C_get, ptr, C_put, ptr, NULL, NULL);
        ((dclimplode_decompressobj_blast*)ptr)->finished.store(true);
        return NULL;
    }

    void start_worker(){
        if(threadActive)return;
        int error = pthread_create(&thread,NULL,C_impl,this);
        if(error)throw std::runtime_error(format("pthread_create() error (%d)", error));
        threadActive = true;
    }

    void join_worker(){
        if(!threadActive)return;
        int error = pthread_join(thread,NULL);
        if(error)throw std::runtime_error(format("pthread_join() error (%d)", error));
        threadActive = false;
        thread = pthread_t();
    }

    bool eof() const{return finished.load() && result == 0;}

    py::bytes decompress(const py::bytes &obj){
        std::unique_lock<std::mutex> lock(apiMutex, std::try_to_lock);
        if(!lock.owns_lock())throw std::runtime_error("concurrent decompressor calls are not supported");
        if(finished.load())throw std::runtime_error("decompressor is already finalized");
        outstr.resize(0);
        {
            char *buffer = nullptr;
            ssize_t length = 0;
            PYBIND11_BYTES_AS_STRING_AND_SIZE(obj.ptr(), &buffer, &length);
            instr = std::string(buffer, length);
            hasInput.store(true);
        }
        {
            py::gil_scoped_release release;
            start_worker();
            for(;hasInput.load() && !finished.load();)usleep(SLEEP_US);
            for(;!requireInput.load() && !finished.load();)usleep(SLEEP_US);
            if(finished.load()){
                join_worker();
                if(result)throw std::runtime_error(format("blast() error (%d)", result));
            }
        }
        return py::bytes((char*)outstr.data(), outstr.size());
    }
};

class dclimplode_decompressobj_pklib{
    std::string instr;
    std::vector<unsigned char>outstr;
    std::atomic<bool> requireInput;
    std::atomic<bool> hasInput;
    std::atomic<bool> finished;
    bool threadActive;
    int result;
    pthread_t thread;
    std::mutex apiMutex;

    size_t offset;
    TDcmpStruct DcmpStruct;
public:
    dclimplode_decompressobj_pklib(): requireInput(false), hasInput(false), finished(false), threadActive(false), result(0), thread(), offset(0){
        outstr.reserve(65536);
    }
    ~dclimplode_decompressobj_pklib(){
        if(threadActive){
            pthread_cancel(thread);
            pthread_join(thread,NULL);
        }
    }

    void put(char *buf, unsigned int len){
        outstr.insert(outstr.end(), buf, buf+len);
    }
    unsigned int get(char *buf, unsigned int size){
        if(offset == instr.size()){
            requireInput.store(true);
            for(;!hasInput.load();){pthread_testcancel();usleep(SLEEP_US);}
            requireInput.store(false);
            offset = 0;
        }
        hasInput.store(false);
        size_t copysize = (std::min)(static_cast<size_t>(size), instr.size()-offset);
        memcpy(buf,instr.data()+offset,copysize);
        offset+=copysize;
        return static_cast<unsigned int>(copysize);
    }
    static void C_put(char *buf, unsigned int *size, void *param){
        ((dclimplode_decompressobj_pklib*)param)->put(buf, *size);
    }
    static unsigned int C_get(char *buf, unsigned int *size, void *param){
        return ((dclimplode_decompressobj_pklib*)param)->get(buf, *size);
    }
    static void* C_impl(void *ptr){
        TDcmpStruct *pDcmpStruct = &((dclimplode_decompressobj_pklib*)ptr)->DcmpStruct;
        memset(pDcmpStruct, 0, sizeof(TDcmpStruct));
        ((dclimplode_decompressobj_pklib*)ptr)->result = explode(C_get, C_put, (char*)pDcmpStruct, ptr);
        ((dclimplode_decompressobj_pklib*)ptr)->finished.store(true);
        return NULL;
    }

    void start_worker(){
        if(threadActive)return;
        int error = pthread_create(&thread,NULL,C_impl,this);
        if(error)throw std::runtime_error(format("pthread_create() error (%d)", error));
        threadActive = true;
    }

    void join_worker(){
        if(!threadActive)return;
        int error = pthread_join(thread,NULL);
        if(error)throw std::runtime_error(format("pthread_join() error (%d)", error));
        threadActive = false;
        thread = pthread_t();
    }

    bool eof() const{return finished.load() && result == 0;}

    py::bytes decompress(const py::bytes &obj){
        std::unique_lock<std::mutex> lock(apiMutex, std::try_to_lock);
        if(!lock.owns_lock())throw std::runtime_error("concurrent decompressor calls are not supported");
        if(finished.load())throw std::runtime_error("decompressor is already finalized");
        outstr.resize(0);
        {
            char *buffer = nullptr;
            ssize_t length = 0;
            PYBIND11_BYTES_AS_STRING_AND_SIZE(obj.ptr(), &buffer, &length);
            if(!threadActive){
                instr.append(buffer, length);
                // PKLIB reads the two-byte header and initial bit-buffer byte directly.
                if(length != 0 && instr.size() < 3)return py::bytes();
            }else{
                instr.assign(buffer, length);
            }
            hasInput.store(true);
        }
        {
            py::gil_scoped_release release;
            start_worker();
            for(;hasInput.load() && !finished.load();)usleep(SLEEP_US);
            for(;!requireInput.load() && !finished.load();)usleep(SLEEP_US);
            if(finished.load()){
                join_worker();
                if(result)throw std::runtime_error(format("explode() error (%d)", result));
            }
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

    py::class_<dclimplode_decompressobj_blast, std::shared_ptr<dclimplode_decompressobj_blast> >(m, "decompressobj_blast")
    .def(py::init<>())
    .def("decompress", &dclimplode_decompressobj_blast::decompress,
     "obj"_a
    )
    .def_property_readonly("eof", &dclimplode_decompressobj_blast::eof)
    ;

    py::class_<dclimplode_decompressobj_pklib, std::shared_ptr<dclimplode_decompressobj_pklib> >(m, "decompressobj_pklib")
    .def(py::init<>())
    .def("decompress", &dclimplode_decompressobj_pklib::decompress,
     "obj"_a
    )
    .def_property_readonly("eof", &dclimplode_decompressobj_pklib::eof)
    ;

    m.attr("CMP_BINARY") = int(CMP_BINARY);
    m.attr("CMP_ASCII") = int(CMP_ASCII);
}
