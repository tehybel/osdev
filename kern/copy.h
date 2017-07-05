/* See COPYRIGHT for copyright information. */

#ifndef JOS_KERN_COPY_H
#define JOS_KERN_COPY_H

void copy_to_user(void *dst, void *src, size_t length);
void copy_from_user(void *dst, void *src, size_t length);


#endif // !JOS_KERN_COPY_H
