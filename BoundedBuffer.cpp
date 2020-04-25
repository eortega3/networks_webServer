/**
 * Implementation of the BoundedBuffer class.
 * See the associated header file (BoundedBuffer.hpp) for the declaration of
 * this class.
 */
#include <cstdio>

#include "BoundedBuffer.hpp"

/**
 * Constructor that sets capacity to the given value. The buffer itself is
 * initialized to en empty queue.
 *
 * @param max_size The desired capacity for the buffer.
 */
BoundedBuffer::BoundedBuffer(int max_size) {
	capacity = max_size;

	// buffer field implicitly has its default (no-arg) constructor called.
	// This means we have a new buffer with no items in it.
}

/**
 * Gets the first item from the buffer then removes it.
 */
int BoundedBuffer::getItem() {
	std::unique_lock<std::mutex> gL(shared_mutex);
	while(count == 0)
	{
		data_available.wait(gL);	
	}
	count--;
	int item = this->buffer.front(); // "this" refers to the calling object...
	buffer.pop(); // ... but like Java it is optional (no this in front of buffer on this line)
	tail++;
	if(tail == capacity)
	{
		tail = 0;
	}
	
	space_available.notify_one();
	gL.unlock();
	return item;
}

/**
 * Adds a new item to the back of the buffer.
 *
 * @param new_item The item to put in the buffer.
 */
void BoundedBuffer::putItem(int new_item) {
	std::unique_lock<std::mutex> pL(shared_mutex);
	while(count == capacity)
	{
		space_available.wait(pL);
	}
	count++;
	buffer.push(new_item);
	head++;
	if(head == capacity)
	{
		head = 0;
	}
	data_available.notify_one();
	pL.unlock();
}
