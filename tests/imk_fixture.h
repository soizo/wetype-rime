#pragma once
#include <cstddef>
void imk_fixture_install();
void imk_fixture_perform(const char *text,size_t selection,size_t marked,size_t marked_length,
                         bool unavailable,void (*action)(void *),void *context);
size_t imk_fixture_reads();
