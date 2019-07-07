#include <iostream>
#include <vector>
#include <cstdlib>
#include <random>
#include <ctime>
#include <cmath>

#define getOffset(pageID) (pageID * 4096 * 2)

using namespace std;
using zjie = short;

// adr范围：0~1Gzjie。 
// 1073741824
// page:4Kzjie
// 4096
// number of page : 1073741824 / 4096 = 262144
// 262144 * 4 = 1048576 byte = 1GB too big
// so 正向映射莫得考虑了

// 反向映射
// 16K / 4K = 4 pages
// 四页，很舒服

class ERROR{};

// 默认大端规则，即高位存在低地址
class MemoryUnit
{
public:
    MemoryUnit() : memory(16384, 0), pageTable(4, 0), pageTimes(4, 0), isDirty(4, 0), totalCount(0), hintCount(0) {
        FILE *fp = fopen("disk", "r");
        // 检测虚拟内存文件是否存在，不存在新建一个
        if(fp == NULL){
            fclose(fp);
            fp = fopen("disk", "wb");
            zjie w = 0;
            for(int i = 0; i < 1073741824; i++)
                fwrite((char *)(&w), sizeof(short), 1, fp);
        }
        fclose(fp);
        for(int i = 0; i < 4; i++) {
            pageTable[i] = i;
        }
    }
    ~MemoryUnit() {
        for(int i = 0; i < 4; i++) {
            // 如果这是脏页，要回写
            if(isDirty[i])
                writePage(i, pageTable[i]);
        }
    }
    int lw(int adr) {
        int offset = adr % 4096;
        int memoryID = getMemoryID(adr);
        // 对齐
        if(offset % 2 == 0) {
            int high = memory[memoryID * 4096 + offset];
            int low = memory[memoryID * 4096 + offset + 1];
            return (high << 16) | (low & 0xFFFF);
        }
        // 没对齐
        else {
            int high = memory[memoryID * 4096 + offset];
            // 低位在另一个块中
            if(offset + 1 >= 4096) {
                memoryID = getMemoryID(adr + 1);
                int low = memory[memoryID * 4096];
                return (high << 16) | (low & 0xFFFF);
            }
            // 在同一个块中
            else {
                int low = memory[memoryID * 4096 + offset + 1];
                return (high << 16) | (low & 0xFFFF);
            }
        }
    }
    double hintRate() {
        return (double)hintCount / totalCount;
    }
    void sw(int adr, int data) {
        int offset = adr % 4096;
        int memoryID = getMemoryID(adr);
        isDirty[memoryID] = 1;
        // 对齐
        if(offset % 2 == 0) {
            memory[memoryID * 4096 + offset] = (data >> 16);
            memory[memoryID * 4096 + offset + 1] = (data & 0xFFFF);
        }
        // 没对齐
        else {
            memory[memoryID * 4096 + offset] = (data >> 16);
            // 低位在另一个块中
            if(offset + 1 >= 4096) {
                memoryID = getMemoryID(adr + 1);
                isDirty[memoryID] = 1;
                memory[memoryID * 4096] = (data & 0xFFFF);
            }
            // 在同一个块中
            else {
                memory[memoryID * 4096 + offset + 1] = (data & 0xFFFF);
            }
        }
    }
    void showPageTable() {
        for(auto i : pageTable)
            cout << i << " ";
        cout << endl;
    }
private:
    // 获得内存页ID
    // 如果不存在会替换一个
    int getMemoryID(int adr) {
        totalCount++;
        int pageID = getPages(adr);
        int memoryID = isHint(pageID);
        if(memoryID != -1) {
            pageTimes[memoryID]++;
            hintCount++;
            return memoryID;
        }
        // 莫得有hint
        else{
            // 被替换的块号
            int goDie = LRU();
            // 如果这是脏页，要回写
            if(isDirty[goDie]) {
                writePage(goDie, pageTable[goDie]);
            }
            loadPage(goDie, pageID);
            pageTable[goDie] = pageID;
            isDirty[goDie] = 0;
            pageTimes[goDie] = 1;
            return goDie;
        }
    }
    int getPages(int adr) const {
        return adr / 4096;
    }
    // return page in memory
    // return -1 if not
    int isHint(int pageID) const {
        for(int i = 0; i < 4; i++)
            if(pageTable[i] == pageID)
                return i;
        return -1;
    }
    // LRU替换策略（暂时未考虑时间影响）
    int LRU() const {
        int index = 0;
        for(int i = 1; i < 4; i++)
            if(pageTimes[i] < pageTimes[index])
                index = i;
        return index;
    }
    // 把内存页载入disk
    void writePage(int memoryID, int pageID) {
        FILE *fp = fopen("disk", "r+");
        fseek(fp, getOffset(pageID), 0);
        fwrite(&memory[memoryID * 4096], sizeof(zjie), 4096, fp);
        fclose(fp);
    }
    // 从disk中载入内存页
    void loadPage(int memoryID, int pageID) {
        FILE *fp = fopen("disk", "rb");
        fseek(fp, getOffset(pageID), 0);
        fread(&memory[memoryID * 4096], sizeof(zjie), 4096, fp);
        fclose(fp);
    }
    vector<zjie> memory;
    vector<int> pageTable;
    vector<int> pageTimes;
    vector<int> isDirty;
    int totalCount;
    int hintCount;
};

// 基于高斯分布与随机跳转的内存访问序列
class RandomSequence
{
public:
    RandomSequence(int size) : last(0) {
        srand(time(0));
        for(int i = 0; i < size; i++)
            sequence.push_back(next());
    }
    vector<int> sequence;
private:
    // 标准正态分布随机数
    double gaussrand()
    {
        static double V1, V2, S;
        static int phase = 0;
        double X;
        
        if ( phase == 0 ) {
            do {
                double U1 = (double)rand() / RAND_MAX;
                double U2 = (double)rand() / RAND_MAX;
                
                V1 = 2 * U1 - 1;
                V2 = 2 * U2 - 1;
                S = V1 * V1 + V2 * V2;
            } while(S >= 1 || S == 0);
            
            X = V1 * sqrt(-2 * log(S) / S);
        } else
            X = V2 * sqrt(-2 * log(S) / S);
            
        phase = 1 - phase;
    
        return X;
    }
    // 有80%概率出以上次结果为期望的随机数
    // 20%概率随机跳转
    // 模拟空间局部性
    int next() {
        int i = abs((int)rand());
        if(i % 10 > 1) {
            last = static_cast<int>(5 * gaussrand() + last);
            if(last < 0)
                last = 0;
            if(last >= 1073741824)
                last = 1073741823;
            return last;
        }
        else 
            return (last = rand() % 1073741824);
    }
    int last;
};

int main()
{
    int size;
    cout << "sequence size: ";
    cin >> size;
    RandomSequence rs(size);
    MemoryUnit MU;
    for(auto i : rs.sequence) {
        MU.sw(i, i);
        // cout << i << endl;
    }
    cout << "Failed logs:" << endl;
    for(auto i : rs.sequence) {
        int content = MU.lw(i);
        // 因为后来写的可能会把它覆盖掉，所以pass条件多出三个
        if(content == i || (content == (((i - 1) & 0xFFFF) << 16 | (i & 0xFFFF))) || (content == ((i & 0xFFFF0000) | (((i + 1) & 0xFFFF0000) >> 16))) || (content == ((((i-1)&0xFFFF) << 16) | ((i+1)&0xFFFF0000) >> 16)))
            // cout << "pass" << endl;
            ;
        else {
            // cout << content << " " << i << " " << (((i - 1) & 0xFFFF) << 16 | (i & 0xFFFF)) << " " << ((i & 0xFFFF0000) | (((i + 1) & 0xFFFF0000) >> 16)) << endl;
            cout << "failed" << endl;
        }
    }
    cout << "--------------" << endl;
    cout << "Hint rate: " << MU.hintRate() << endl;
    return 0;
}