///@doc 分段hash表 + 公共溢出池实现
///兼容STL算法

#ifndef HASHTABLE_SEGMENT_MAP_HPP
#define HASHTABLE_SEGMENT_MAP_HPP

#include <stddef.h>
#include <limits>
#include <utility>


/**
 * 容器的内存结构是连续的.
 * NOT MT-safe
 * @brief 多阶hash, 如果hash表还是无法存储则存储在一个公共的溢出池,
 *        主要用于超大数据量hash操作
 * @rehash
 * 当公共溢出池超过指定阈值时会重新hash,可以选择固定大小table,则存在无法插入情况
 *
 * @param Key 建类型
 * @param T 值类型
 * @param 代表空键的值，用于无对象标记
 */

template <typename Key,
          typename T,
          Key NUL_KEY=Key()
         >
class SegmentMap
{
 public:
  // member types like STL
  typedef Key key_type;
  typedef T value_type;

 public:
  enum { npos = -1 };

 public:
  /**
  * @param KEY_TYPE key类型,必须为整形数值类型
  * @param T 值类型 1. T必须是 POD 类型，Copyable !!!. 如果不是后果自负
  *                 2. 有默认构造函数,默认构造的对象getKey 必须返回NIL_KEY
  *                 3. 必须实现 (Conecpt)
  *                    a. KEY_TYPE getKey() const
  *                    b. void setKey(KEY_TYPE key) 方法
  *
  * @param MAP_SIZE 存储元素的上线. 实际的最大存储数量<= MAP_SIZE
  * @param STAGE
  * 阶数量，一般在20~50，数量越大利用率越高，但是查找速度越慢.反之依然.
  * @param NIL_KEY 被认为是空元素的Key值。元素的key不能为NIL_KEY
  */
  SegmentMap(size_t slot_count, int segment_count) { init(); }

 private:
  SegmentMap(const SegmentMap &);
  void operator=(const SegmentMap &);

 private:
  class _Iter {
   public:
    _Iter(container_type *c, size_t index) : _continer(c), _index(index) {}

    _Iter &operator++(void) {
      for (size_t i = _index + 1; i < _continer->_max_size; ++i) {
        if (_continer->_bucket_slots[i].getKey() != NIL_KEY) {
          _index = i;
          return *this;
        }
      }

      _index = npos;
      return *this;
    }

    _Iter operator++(int) {
      size_t old_index = _index;
      for (size_t i = _index + 1; i < _continer->_max_size; ++i) {
        if (_continer->_bucket_slots[i].getKey() != NIL_KEY) {
          _index = i;
          return _Iter(this, old_index);
        }
      }

      _index = npos;
      return _Iter(this, old_index);
    }

    void erase() {
      if (_continer->bitmap.test(_index)) {
        _continer->bitmap.reset(_index);
        --_continer->_used_size;
      }
    }

    T &operator*() { return _continer->_bucket_slots[_index]; }

    T *operator->() { return _continer->_bucket_slots + _index; }

    bool operator==(const _Iter &o) const {
      return _index == o._index && _continer == o._continer;
    }

    bool operator!=(const _Iter &o) const { return not operator==(o); }

    const size_t index() const { return _index; }
    size_t index() { return _index; }

   private:
    container_type *_continer;
    size_t _index;

    friend container_type;
  };

 public:
  typedef _Iter iterator;
  // iterator
  iterator begin() {
    if (empty()) return end();

    // find first
    for (size_t i = 0; i < _max_size; ++i) {
      if (_bucket_slots[i].getKey() != NIL_KEY) return _Iter(this, i);
    }

    return end();  // can not run here
  }

  iterator end() { return _Iter(this, npos); }

  iterator end() const { return _Iter(this, npos); }

  size_t stage() const { return STAGE; }

  bool empty() const { return _used_size == 0; }

  size_t size() const { return _used_size; }

  size_t max_size() const { return _max_size; }

  /**
   * 返回使用率
   */
  float used_rate() const { return (float)_used_size / _max_size; }

 public:
  // modifiers

  T &operator[](iterator it) { return _bucket_slots[it.index()]; }

  /**
   * clear the contents. same as STL map.clear()
   */
  void clear() {
    // ScopeWLock lock(&_buckets[STAGE-1]._rwlock);
    for (_Iter it = begin(); it != end(); ++it) {
      it->setKey((Key)NIL_KEY);
    }
    _used_size = 0;
  }

  bool isInit() const { return _isInit; }

  /**
   * 查找元素是否存在
   * @return 返回元素迭代器，如果不存在返回end()
   */
  iterator find(const Key key) {
    if (key == NIL_KEY)  // unlikely
      return end();

    size_t index = npos;
    for (size_t i = 0; i < STAGE; ++i) {
      index = (key % _buckets[i].size) + _buckets[i].offset;
      if (_bucket_slots[index].getKey() == key) {
        return iterator(this, index);
      }
    }

    return end();
  }

  /**
   * same STL map.count
   */
  size_t count(const Key key) {
    if (find(key).index() != npos)
      return 1;
    else
      return 0;
  }

  /**
   * inser a element.
   * 元素的key不能为NIL_KEY
   * @return 返回元素在内存中的索引，和是否插入成功。
   * 1. 如果无法插入返回(end(), false)
   * 2. 元素已经存在，返回(已存在元素的下标,false);
   * 3. 插入成功，返回（新元素的下标，true)
   *
   */
  std::pair<iterator, bool> insert_new(const T &v) {
    const Key key = v.getKey();

    if (key == NIL_KEY)  // unlikely
      return {end(), false};

    size_t index = npos;
    for (size_t i = 0; i < STAGE; ++i) {
      index = (key % _buckets[i].size) + _buckets[i].offset;
      // ScopeWLock lock(&_buckets[i]._rwlock);
      Key slotKey = _bucket_slots[index].getKey();
      if (slotKey != NIL_KEY) {
        if (slotKey == key)  // find same element
          return {iterator(this, index), false};

#ifdef TEST_SegmentMap
        ++_find_count;
#endif
        continue;  // not same
      }

      // find empty slot insert
      _bucket_slots[index] = v;
      ++_used_size;

      return {iterator(this, index), true};
    }

    // no empty space insert
    return {end(), false};
  }

  /**
   * inser a element,
   * 如果无法插入则会通过选择函数找到一个替换的元素进行替换,一定会插入成功.
   * 一般用于实现淘汰插入.
   * 元素的key不能为NIL_KEY,
   * @param fn 替代选择函数 bool fn(const T&l, const T&r);
   *  当无法插入时，会把所有与插入对象位置冲突的元素进行调用，在这些元素中选择一个最适合替换的进行淘汰，
   *  接受两个与插入元素冲突的元素,返回true表示第一次对象被淘汰，false表示第二个对象被淘汰.
   * @note 插入元素不会与冲突元素进行比较，这样保证插入元素肯定会被插入
   * @param [out] replaced 不为NULL时，返回被替换的对象;
   * @return 返回元素在内存中的索引，和是否进行了替换
   */
  // TODO test
  template <typename Fn>
  std::pair<iterator, bool> insert_or_replace(const T &v, Fn fn,
                                              T *replaced = NULL /* out */) {
    const Key key = v.getKey();

    if (key == NIL_KEY)  // unlikely
      return {end(), false};

    size_t index = npos;
    for (size_t i = 0; i < STAGE; ++i) {
      index = (key % _buckets[i].size) + _buckets[i].offset;
      // ScopeWLock lock(&_buckets[i]._rwlock);
      Key slotKey = _bucket_slots[index].getKey();
      if (slotKey != NIL_KEY) {
        if (slotKey == key)  // find same element
          return {iterator(this, index), false};

#ifdef TEST_SegmentMap
        ++_find_count;
#endif
        continue;  // not same
      }

      // find empty slot insert
      _bucket_slots[index] = v;
      ++_used_size;

      return {iterator(this, index), false};
    }

    // no empty space insert, replace one
    index = npos;
    size_t leftIndex =
        (key % _buckets[STAGE - 1].size) + _buckets[STAGE - 1].offset;
    for (size_t i = 0; i < STAGE - 1; ++i) {
      index = (key % _buckets[i].size) + _buckets[i].offset;
      if (not fn(_bucket_slots[leftIndex], _bucket_slots[index])) {
        leftIndex = index;
      }
    }

    if (replaced) *replaced = _bucket_slots[index];

    _bucket_slots[leftIndex] = v;

    return {iterator(this, leftIndex), true};
  }

  //兼容STL, same insert_new/1
  std::pair<iterator, bool> insert(const T &v) { return insert_new(v); }

  std::pair<iterator, bool> insert(const T &v, iterator &it) {
    _bucket_slots[it.index()] = v;
    return {it, true};
  }

  /**
   * 当元素不存在时插入，当已经有相同元素是替换为新元素。
   * @return 返回元素的下标，如果bool是true则表示插入，如果是false则表示替换
   * 当返回npos时，表示无法插入
   */
  std::pair<iterator, bool> insert_or_update(const T &v) {
    const Key &key = v.getKey();

    if (key == NIL_KEY)  // unlikely
      return {end(), false};

    size_t index = npos;
    for (size_t i = 0; i < STAGE; ++i) {
      // check bitmap[rom + offset] is set
      index = (key % _buckets[i].size) + _buckets[i].offset;
      // ScopeWLock lock(&_buckets[i]._rwlock);
      Key slotKey = _bucket_slots[index].getKey();
      if (slotKey != NIL_KEY) {
        if (slotKey == key) {  // find same element,update
          // write Lock
          _bucket_slots[index] = v;
          return {iterator(this, index), false};
        }

// not same find next stage
#ifdef TEST_SegmentMap
        ++_find_count;
#endif
        continue;
      }

      // find empty slot insert
      _bucket_slots[index] = v;
      ++_used_size;
      return {iterator(this, index), true};
    }

    // no empty space insert
    return {end(), false};
  }

  /**
   * 删除一个元素.
   * @return 返回是否删除成功。（是否有该元素)
   */
  bool erase(const Key key) {
    size_t index = find(key).index();
    if (index == npos) return false;

    // ScopeWLock lock(&_buckets[STAGE-1]._rwlock);
    _bucket_slots[index].setKey(NIL_KEY);
    --_used_size;

    return true;
  }

  void erase(const iterator &it) {
    size_t index = it.index();

    // ScopeWLock lock(&_buckets[STAGE-1]._rwlock);
    _bucket_slots[index].setKey(NIL_KEY);
    --_used_size;
  }

  int init() {
    //计算stage大小
    // xxxxxxxxxxxxxxxxxx
    // xxxxxxxxxxxxxxx
    // xxxxxxxxxxx
    // xxxxx
    //每一阶的大小为素数,数量依次递减,
    //目前使用素数序列，
    // TODO
    //数量的选择采用等比数列 减少的系数为1.3

    size_t used = 0;
    size_t size = MAP_SIZE / STAGE;
    for (size_t i = 1; i < STAGE; ++i) {
      size = find_perv_prime(size);
      _buckets[i].size = size;
      used += size;
    }
    size = MAP_SIZE - used;
    size = find_perv_prime(size);
    _buckets[0].size = size;

    // clc offset
    used = 0;
    for (size_t i = 0; i < STAGE; ++i) {
      _buckets[i].offset = used;
      used += _buckets[i].size;
    }

    _max_size = used;
    _isInit = true;
    _used_size = 0;
    return 0;
  }

#ifdef TEST_SegmentMap
 public:
  static void test();
  size_t _find_count = 0;
#else
 private:
#endif

  // struct ScopeWLock
  //{
  //    ScopeWLock(pthread_rwlock_t *l)
  //        : _l(l)
  //    {
  //        pthread_rwlock_wrlock(_l);
  //    }

  //    ~ScopeWLock() { pthread_rwlock_unlock(_l); }

  //    pthread_rwlock_t *_l;
  //};

  struct {
    //根据MAP_SIZE 自动选择是uint16_t 还是uint32_t
    // using uint = typename std::conditional<MAP_SIZE <
    // std::numeric_limits<uint16_t>::max(),
    //      uint16_t,
    //      uint32_t>::type;

    uint32_t size;
    uint32_t offset;
    // pthread_rwlock_t _rwlock; //每一个bucket一个锁
  } _buckets[STAGE];  //存储每阶段大小,和偏移值

  bool _isInit;
  size_t _max_size;   //总元素数量
  size_t _used_size;  //当前的元素个数

  T _bucket_slots[MAP_SIZE];
};

#endif  // HASHTABLE_SEGMENT_MAP_HPP
