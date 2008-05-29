/*
 * src/AtomSpace/HandleEntry.cc
 *
 * Copyright (C) 2002-2007 Novamente LLC
 * All Rights Reserved
 *
 * Written by Thiago Maia <thiago@vettatech.com>
 *            Andre Senna <senna@vettalabs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License v3 as
 * published by the Free Software Foundation and including the exceptions
 * at http://opencog.org/wiki/Licenses
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program; if not, write to:
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "HandleEntry.h"
#include "ClassServer.h"
#include "exceptions.h"
#include "Link.h"
#include "Node.h"
#include "TLB.h"
#include "CoreUtils.h"
#include "CompositeTruthValue.h"

int HandleEntry::existingObjects = 0;

HandleEntry::HandleEntry(Handle handle)
{

    ++existingObjects;
    // linked-list with no head cell
    this->handle = handle;
    next = NULL;
}

HandleEntry::~HandleEntry()
{

    --existingObjects;
    // recursion is not used to avoid stack overflow on large lists
    if (next != NULL) {
        HandleEntry *p, *q;
        p = q = next;
        while (p->next != NULL) {
            p = p->next;
            q->next = NULL;
            delete q;
            q = p;
        }
        delete p;
    }
}

Atom* HandleEntry::getAtom()
{
    return TLB::getAtom(handle);
}

HandleEntry* HandleEntry::clone()
{

    if (this == NULL) return(NULL);

    HandleEntry *answer, *p, *q;

    // answer keeps the cloned list head while p is used to iterate in the
    // cloned list
    answer = p = new HandleEntry(handle);
    q = this->next;
    while (q != NULL) {
        p->next = new HandleEntry(q->handle);
        p = p->next;
        q = q->next;
    }
    return answer;
}

int HandleEntry::getSize()
{

    HandleEntry *current = this;
    int size = 0;
    // O(n) to get list size
    while (current != NULL) {
        current = current->next;
        size++;
    }
    return size;
}

HandleEntry* HandleEntry::last()
{
    HandleEntry *current = this;
    // O(n) to get last member
    while (current->next != NULL) {
        current = current->next;
    }
    return current;
}

bool HandleEntry::contains(Handle h)
{
    HandleEntry *current = this;
    while (current != NULL) {
        if (current->handle == h)
            return true;
        current = current->next;
    }
    return false;
}

HandleEntry* HandleEntry::intersection(HandleEntry* set1, HandleEntry* set2)
{

    std::vector<HandleEntry*> sets(2);
    sets[0] = set1;
    sets[1] = set2;
    return intersection(sets);
}

HandleEntry* HandleEntry::remove(HandleEntry* set, Handle h)
{
    HandleEntry* buffer;
    // The search for invalid elements need to be done in two steps because
    // invalid elements found in the middle of the list need to be treated
    // differently from invalid elements found in its begining.
    while ((set != NULL) &&
            (set->handle == h)) {
        buffer = set;
        set = set->next;
        buffer->next = NULL;
        delete buffer;
    }
    if (set == NULL) return NULL;
    HandleEntry* head = set;
    while (set->next != NULL) {
        if (set->next->handle == h) {
            buffer = set->next;
            set->next = set->next->next;
            buffer->next = NULL;
            delete buffer;
        } else {
            set = set->next;
        }
    }
    // head contains the filtered list.
    return head;
}

HandleEntry* HandleEntry::intersection(std::vector<HandleEntry*>& sets) throw (InconsistenceException)
{

    // leave this method if there are no lists
    if (sets.size() == 0) {
        return NULL;
    }

    // if there is at least one null list, intersection is empty
    for (unsigned int i = 0; i < sets.size(); i++) {
        if (sets[i] == NULL) {
            for (unsigned int j = 0; j < sets.size(); j++) {
                if (sets[j] != NULL) {
                    delete sets[j];
                }
            }
            return NULL;
        }
    }

    Handle** v = new Handle*[sets.size()];
    int* sizeArray = new int[sets.size()];

    // The set of linked-lists is transformed into a set of sorted arrays.
    // Each list is traversed and its elements are put into a regular array
    // which is sorted before being passed to the actual intersection
    // computation algorithm. Once the lists are transformed, they are deleted.
    for (unsigned int i = 0; i < sets.size(); i++) {
        sizeArray[i] = sets[i]->getSize();
        v[i] = new Handle[sizeArray[i]];

        // linked-list linearization
        HandleEntry* current = sets[i];
        int j = 0;
        while (current != NULL) {
            v[i][j++] = current->handle;
            current = current->next;
        }

        if (j != sizeArray[i]) {
            throw RuntimeException(TRACE_INFO, "consistency check failed for intersection");
        }

        // sets[i] is destroyed after v is set.
        delete sets[i];

        // each list is ordered, to make intersection algorithm cheaper.
        qsort(v[i], sizeArray[i], sizeof(Handle), CoreUtils::handleCompare);
    }

    // this call will actually compute the intersection
    return intersection(v, sizeArray, sets.size());
}

int HandleEntry::nextMatch(Handle** sets, int* sizes, std::vector<int>& cursors)
{

    int n = cursors.size();
    for (;;) {

        bool fail = false;
        Handle max = (Handle) ((unsigned int) 0); // this is zero, not null (the smallest possible pointer)
        // if current positions stored in cursors is not a match, the largest
        // value pointed by cursors is computed to reposition cursors
        // accordingly
        for (int j = 0; j < n; j++) {
            // overflow in one of the arrays: no more matches
            if (cursors[j] >= sizes[j]) {
                return -1;
            }
            if (CoreUtils::handleCompare(&sets[j][cursors[j]], &max) > 0) {
                max = sets[j][cursors[j]];
            }
            if (CoreUtils::handleCompare(&sets[0][cursors[0]], &sets[j][cursors[j]])) {
                fail = true;
                // can't break here in order to compute max
            }
        }


        if (fail) {
            // current position is not a match, so cursors are updated to point
            // to the next element in each array which is greater than or
            // equals to max
            for (int j = 0; j < n; j++) {
                while (CoreUtils::handleCompare(&sets[j][cursors[j]], &max) < 0) {
                    cursors[j]++;
                    // overflow in one of the arrays: no more matches
                    if (cursors[j] >= sizes[j]) {
                        return -1;
                    }
                }
            }
        } else {
            // current position is a match so all cursors are incremented of 1
            for (int j = 0; j < n; j++) {
                cursors[j]++;
            }
            return cursors[0] - 1;
        }
    }
}

HandleEntry* HandleEntry::intersection(Handle** sets, int* sizes, int n)
{

    HandleEntry *answer = NULL, *current = NULL;

    // there is one cursor for each list.
    std::vector<int> cursors(n, 0);
    int pos;

    while ((pos = nextMatch(sets, sizes, cursors)) >= 0) {
        if (answer == NULL) {
            answer = current = new HandleEntry(sets[0][pos]);
        } else {
            current->next = new HandleEntry(sets[0][pos]);
            current = current->next;
        }
    }

    for (int i = 0; i < n; i++) {
        delete[](sets[i]);
    }
    delete[](sets);
    delete[](sizes);

    return answer;
}

HandleEntry* HandleEntry::concatenation(HandleEntry* set1, HandleEntry* set2)
{
    if (set1 == NULL) return set2;

    if (set2 != NULL) {
        // it scans the first list until the last element is reached, and then
        // it makes the next of the last element point to the first element of the
        // second list.
        HandleEntry* current = set1;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = set2;
    }

    return set1;
}

HandleEntry* HandleEntry::filterSet(HandleEntry* set, bool deleted)
{

    HandleEntry* buffer;

    // The search for invalid elements need to be done in two steps because
    // invalid elements found in the middle of the list need to be treated
    // differently from invalid elements found in its begining.

    while ((set != NULL) &&
            set->getAtom()->isMarkedForRemoval() != deleted) {
        buffer = set;
        set = set->next;
        buffer->next = NULL;
        delete buffer;
    }

    if (set == NULL) return NULL;


    HandleEntry* head = set;
    while (set->next != NULL) {
        if (set->getAtom()->isMarkedForRemoval() != deleted) {
            buffer = set->next;
            set->next = set->next->next;
            buffer->next = NULL;
            delete buffer;
        } else {
            set = set->next;
        }
    }

    // head contains the filtered list.
    return head;
}

HandleEntry* HandleEntry::filterSet(HandleEntry* set, Arity arity)
{

    HandleEntry* buffer;

    // The search for invalid elements need to be done in two steps because
    // invalid elements found in the middle of the list need to be treated
    // differently from invalid elements found in its begining.

    while ((set != NULL) &&
            set->getAtom()->getArity() != arity) {
        buffer = set;
        set = set->next;
        buffer->next = NULL;
        delete buffer;
    }

    if (set == NULL) return NULL;


    HandleEntry* head = set;
    while (set->next != NULL) {
        if (set->getAtom()->getArity() != arity) {
            buffer = set->next;
            set->next = set->next->next;
            buffer->next = NULL;
            delete buffer;
        } else {
            set = set->next;
        }
    }

    // head contains the filtered list.
    return head;
}


HandleEntry* HandleEntry::filterSet(HandleEntry* set, const char* name)
{

    HandleEntry* buffer;

    // The search for invalid elements need to be done in two steps because
    // invalid elements found in the middle of the list need to be treated
    // differently from invalid elements found in its begining.

    while ((set != NULL) &&
            ((ClassServer::isAssignableFrom(LINK, set->getAtom()->getType())) ||
             (ClassServer::isAssignableFrom(NODE, set->getAtom()->getType()) &&
              (strcmp(((Node*) set->getAtom())->getName().c_str(), name))))) {
        buffer = set;
        set = set->next;
        buffer->next = NULL;
        delete buffer;
    }

    if (set == NULL) return NULL;

    HandleEntry* head = set;
    while (set->next != NULL) {
        Atom *itAtom = set->next->getAtom();
        if (((ClassServer::isAssignableFrom(LINK, itAtom->getType())) ||
                (ClassServer::isAssignableFrom(NODE, itAtom->getType()) &&
                 (strcmp(((Node*) itAtom)->getName().c_str(), name))))) {
            buffer = set->next;
            set->next = set->next->next;
            buffer->next = NULL;
            delete buffer;
        } else {
            set = set->next;
        }
    }

    // head contains the filtered list.
    return head;
}

HandleEntry* HandleEntry::filterSet(HandleEntry* set, Type type, bool subclass)
{

    if ((type == ATOM) && (subclass == true)) {
        return set;
    }

    HandleEntry* buffer;

    // The search for invalid elements need to be done in two steps because
    // invalid elements found in the middle of the list need to be treated
    // differently from invalid elements found in its begining.

    while ((set != NULL) &&
            ((!subclass && (type != set->getAtom()->getType())) ||
             (subclass && !ClassServer::isAssignableFrom(type, set->getAtom()->getType())))) {
        buffer = set;
        set = set->next;
        buffer->next = NULL;
        delete buffer;
    }

    if (set == NULL) return NULL;

    HandleEntry* head = set;
    while (set->next != NULL) {
        if ((!subclass && (type != set->next->getAtom()->getType())) ||
                (subclass && !ClassServer::isAssignableFrom(type, set->next->getAtom()->getType()))) {
            buffer = set->next;
            set->next = set->next->next;
            buffer->next = NULL;
            delete buffer;
        } else {
            set = set->next;
        }
    }

    // head contains the filtered list.
    return head;
}

HandleEntry* HandleEntry::filterSet(HandleEntry* set, const char* name, Type type, bool subclass)
{

    if ((name != NULL) && (!ClassServer::isAssignableFrom(NODE, type))) {
        delete set;
        return NULL;
    }

    HandleEntry* buffer;

    // The search for invalid elements need to be done in two steps because
    // invalid elements found in the middle of the list need to be treated
    // differently from invalid elements found in its begining.

    while ((set != NULL) &&
            ((!subclass && (type != set->getAtom()->getType())) ||
             (subclass && !ClassServer::isAssignableFrom(type, set->getAtom()->getType())) ||
             ((name != NULL) && (ClassServer::isAssignableFrom(NODE, set->getAtom()->getType())) &&
              (strcmp(((Node*) set->getAtom())->getName().c_str(), name))))) {

        buffer = set;
        set = set->next;
        buffer->next = NULL;
        delete buffer;
    }

    if (set == NULL) return NULL;

    HandleEntry* head = set;

    // The search for invalid elements need to be done in two steps because
    // invalid elements found in the middle of the list need to be treated
    // differently from invalid elements found in its begining.

    while (set->next != NULL) {
        if ((!subclass && (type != set->next->getAtom()->getType())) ||
                (subclass && !ClassServer::isAssignableFrom(type, set->next->getAtom()->getType())) ||
                ((name != NULL) && (ClassServer::isAssignableFrom(NODE, set->getAtom()->getType())) &&
                 (strcmp(((Node*) set->getAtom())->getName().c_str(), name)))) {
            buffer = set->next;
            set->next = set->next->next;
            buffer->next = NULL;
            delete buffer;
        } else {
            set = set->next;
        }
    }

    // head contains the filtered list.
    return head;
}

HandleEntry* HandleEntry::filterSet(HandleEntry* set, Handle handle, Arity arity)
{
    // TODO: What is the arity parameter for?

    HandleEntry* buffer = TLB::getAtom(handle)->getIncomingSet()->clone();

    return(intersection(set, buffer));
}


HandleEntry* HandleEntry::filterSet(HandleEntry* set, Handle handle, Arity position, Arity arity)
{

    HandleEntry* buffer;

    // The search for invalid elements need to be done in two steps because
    // invalid elements found in the middle of the list need to be treated
    // differently from invalid elements found in its begining.

    while ((set != NULL) &&
            ((set->getAtom()->getArity() != arity) ||
             (position >= arity) ||
             (set->getAtom()->getOutgoingSet()[position] != handle))) {
        buffer = set;
        set = set->next;
        buffer->next = NULL;
        delete buffer;
    }
    if (set == NULL) return NULL;

    HandleEntry* head = set;
    while (set->next != NULL) {
        if ((set->next->getAtom()->getArity() != arity) ||
                (position >= arity) ||
                (set->next->getAtom()->getOutgoingSet()[position] != handle)) {
            buffer = set->next;
            set->next = set->next->next;
            buffer->next = NULL;
            delete buffer;
        } else {
            set = set->next;
        }
    }

    // head contains the filtered list.
    return head;
}

HandleEntry* HandleEntry::filterSet(HandleEntry* set, Type type, bool subclass, Arity arity)
{

    HandleEntry* buffer;
    Type target;

    // The search for invalid elements need to be done in two steps because
    // invalid elements found in the middle of the list need to be treated
    // differently from invalid elements found in its begining.

    while (set != NULL) {
        if (set->getAtom()->getArity() != arity) {
            buffer = set;
            set = set->next;
            buffer->next = NULL;
            delete buffer;
        } else {
            int position;
            for (position = 0; position < arity; position++) {
                if ((target = set->getAtom()->getOutgoingAtom(position)->getType()) &&
                        ((!subclass && (type == target)) ||
                         (subclass && ClassServer::isAssignableFrom(type, target)))) {
                    break;
                }
            }

            if (position == arity) {
                buffer = set;
                set = set->next;
                buffer->next = NULL;
                delete buffer;
            } else {
                break;
            }
        }
    }

    if (set == NULL) return NULL;

    HandleEntry* head = set;
    while (set->next != NULL) {
        if (set->next->getAtom()->getArity() != arity) {
            buffer = set->next;
            set->next = set->next->next;
            buffer->next = NULL;
            delete buffer;
        } else {
            int position;
            for (position = 0; position < arity; position++) {
                if ((target = set->next->getAtom()->getOutgoingAtom(position)->getType()) &&
                        ((!subclass && (type == target)) ||
                         (subclass && ClassServer::isAssignableFrom(type, target)))) {
                    break;
                }
            }

            if (position == arity) {
                buffer = set->next;
                set->next = set->next->next;
                buffer->next = NULL;
                delete buffer;
            } else {
                set = set->next;
            }
        }

    }

    // head contains the filtered list.
    return head;
}


HandleEntry* HandleEntry::filterSet(HandleEntry* set, Type type, bool subclass, Arity position, Arity arity)
{

    HandleEntry* buffer;
    Type target;

    // The search for invalid elements need to be done in two steps because
    // invalid elements found in the middle of the list need to be treated
    // differently from invalid elements found in its begining.

    while ((set != NULL) &&
            ((set->getAtom()->getArity() != arity) ||
             (target = set->getAtom()->getOutgoingAtom(position)->getType(),
              ((!subclass && (type != target)) ||
               (subclass && !ClassServer::isAssignableFrom(type, target)))))) {

        buffer = set;
        set = set->next;
        buffer->next = NULL;
        delete buffer;
    }

    if (set == NULL) return NULL;

    HandleEntry* head = set;
    while (set->next != NULL) {
        if (((set->next->getAtom()->getArity() != arity) ||
                (target = set->next->getAtom()->getOutgoingAtom(position)->getType(),
                 ((!subclass && (type != target)) ||
                  (subclass && !ClassServer::isAssignableFrom(type, target)))))) {
            buffer = set->next;
            set->next = set->next->next;
            buffer->next = NULL;
            delete buffer;
        } else {
            set = set->next;
        }
    }

    // head contains the filtered list.
    return head;
}

HandleEntry* HandleEntry::filterSet(HandleEntry* set, const char* name, Type type, bool subclass, Arity position, Arity arity)
{

    HandleEntry* buffer;
    Type target;

    // The search for invalid elements need to be done in two steps because
    // invalid elements found in the middle of the list need to be treated
    // differently from invalid elements found in its begining.

    while ((set != NULL) &&
            ((set->getAtom()->getArity() != arity) ||
             (target = set->getAtom()->getOutgoingAtom(position)->getType(),
              ((!subclass && (type != target)) ||
               (subclass && !ClassServer::isAssignableFrom(type, target)))) ||
             (ClassServer::isAssignableFrom(NODE, target) ?
              (strcmp(name, ((Node*) set->getAtom()->getOutgoingAtom(position))->getName().c_str())) :
              name != NULL))) {

        buffer = set;
        set = set->next;
        buffer->next = NULL;
        delete buffer;
    }

    if (set == NULL) return NULL;

    HandleEntry* head = set;
    while (set->next != NULL) {
        if (((set->next->getAtom()->getArity() != arity) ||
                (target = set->next->getAtom()->getOutgoingAtom(position)->getType(),
                 ((!subclass && (type != target)) ||
                  (subclass && !ClassServer::isAssignableFrom(type, target)))) ||
                (ClassServer::isAssignableFrom(NODE, target) ?
                 (strcmp(name, ((Node*) set->getAtom()->getOutgoingAtom(position))->getName().c_str())) :
                 name != NULL))) {
            buffer = set->next;
            set->next = set->next->next;
            buffer->next = NULL;
            delete buffer;
        } else {
            set = set->next;
        }
    }

    // head contains the filtered list.
    return head;
}

HandleEntry* HandleEntry::filterSet(HandleEntry* set, const char* name, Arity position, Arity arity)
{

    HandleEntry* buffer;

    // The search for invalid elements need to be done in two steps because
    // invalid elements found in the middle of the list need to be treated
    // differently from invalid elements found in its begining.

    while ((set != NULL) &&
            ((set->getAtom()->getArity() != arity) ||
             (ClassServer::isAssignableFrom(NODE, set->next->getAtom()->getOutgoingAtom(position)->getType()) ?
              (strcmp(name, ((Node*) set->getAtom()->getOutgoingAtom(position))->getName().c_str())) :
              name != NULL))) {

        buffer = set;
        set = set->next;
        buffer->next = NULL;
        delete buffer;
    }

    if (set == NULL) return NULL;

    HandleEntry* head = set;
    while (set->next != NULL) {
        if (((set->next->getAtom()->getArity() != arity) ||
                (ClassServer::isAssignableFrom(NODE, set->next->getAtom()->getOutgoingAtom(position)->getType()) ?
                 (strcmp(name, ((Node*) set->getAtom()->getOutgoingAtom(position))->getName().c_str())) :
                 name != NULL))) {
            buffer = set->next;
            set->next = set->next->next;
            buffer->next = NULL;
            delete buffer;
        } else {
            set = set->next;
        }
    }

    // head contains the filtered list.
    return head;
}

HandleEntry* HandleEntry::filterSet(HandleEntry* set, AttentionValue::sti_t lowerBound, AttentionValue::sti_t upperBound)
{

    HandleEntry* buffer;

    // The search for invalid elements need to be done in two steps because
    // invalid elements found in the middle of the list need to be treated
    // differently from invalid elements found in its begining.

    while ((set != NULL) &&
            ((set->getAtom()->getAttentionValue().getSTI() < lowerBound) ||
             (set->getAtom()->getAttentionValue().getSTI() > upperBound))) {
        buffer = set;
        set = set->next;
        buffer->next = NULL;
        delete buffer;
    }

    if (set == NULL) return NULL;

    HandleEntry* head = set;
    while (set->next != NULL) {
        if ((set->next->getAtom()->getAttentionValue().getSTI() < lowerBound) ||
                (set->next->getAtom()->getAttentionValue().getSTI() > upperBound)) {
            buffer = set->next;
            set->next = set->next->next;
            buffer->next = NULL;
            delete buffer;
        } else {
            set = set->next;
        }
    }

    // head contains the filtered list.
    return head;
}


HandleEntry* HandleEntry::filterSet(HandleEntry* set, VersionHandle vh)
{

    HandleEntry* head = set;
    if (!isNullVersionHandle(vh)) {
        HandleEntry* buffer;

        // The search for invalid elements need to be done in two steps because
        // invalid elements found in the middle of the list need to be treated
        // differently from invalid elements found in its begining.

        while ( (set != NULL) &&
                (set->getAtom()->getTruthValue().getType() != COMPOSITE_TRUTH_VALUE ||
                 ((const CompositeTruthValue&) set->getAtom()->getTruthValue()).getVersionedTV(vh).isNullTv())
              ) {
            buffer = set;
            set = set->next;
            buffer->next = NULL;
            delete buffer;
        }

        if (set == NULL) return NULL;

        head = set;
        while (set->next != NULL) {
            if (set->next->getAtom()->getTruthValue().getType() != COMPOSITE_TRUTH_VALUE ||
                    ((const CompositeTruthValue&) set->next->getAtom()->getTruthValue()).getVersionedTV(vh).isNullTv()) {
                buffer = set->next;
                set->next = set->next->next;
                buffer->next = NULL;
                delete buffer;
            } else {
                set = set->next;
            }
        }
    }

    // head contains the filtered list.
    return head;
}

bool matchesFilterCriteria(Atom* atom, Type targetType, bool targetSubclasses, VersionHandle vh)
{
    bool result = false;
    for (int i = 0; i < atom->getArity() && !result; i++) {
        const Atom* target = atom->getOutgoingAtom(i);
        //printf("Checking atom with TYPE = %s, TV = %s\n", ClassServer::getTypeName(target->getType()), target->getTruthValue().toString().c_str());
        if (target->getTruthValue().getType() == COMPOSITE_TRUTH_VALUE &&
                !((const CompositeTruthValue&) target->getTruthValue()).getVersionedTV(vh).isNullTv()) {
            if (targetSubclasses) {
                result = ClassServer::isAssignableFrom(targetType, target->getType());
            } else {
                result = targetType == target->getType();
            }
        }
    }
    //printf("matchesFilterCriteria (%s, %s, %s, [%p, %s]) returning %s\n",
    //atom->toString().c_str(),
    //ClassServer::getTypeName(targetType),
    //targetSubclasses?"true":"false",
    //vh.substantive,
    //INDICATOR_TYPES[vh.indicator],
    //result?"true":"false");
    return result;
}

HandleEntry* HandleEntry::filterSet(HandleEntry* set, Type targetType, bool targetSubclasses, VersionHandle vh)
{

    HandleEntry* head = set;
    if (!isNullVersionHandle(vh)) {
        HandleEntry* buffer;

        // The search for invalid elements need to be done in two steps because
        // invalid elements found in the middle of the list need to be treated
        // differently from invalid elements found in its begining.

        while ( (set != NULL) && !matchesFilterCriteria(set->getAtom(), targetType, targetSubclasses, vh)) {
            buffer = set;
            set = set->next;
            buffer->next = NULL;
            delete buffer;
        }

        if (set == NULL) return NULL;

        head = set;
        while (set->next != NULL) {
            if (!matchesFilterCriteria(set->next->getAtom(), targetType, targetSubclasses, vh)) {
                buffer = set->next;
                set->next = set->next->next;
                buffer->next = NULL;
                delete buffer;
            } else {
                set = set->next;
            }
        }
    }

    // head contains the filtered list.
    return head;
}

bool matchesFilterCriteria(Atom* atom, const char* targetName, Type targetType, VersionHandle vh)
{
    bool result = false;
    for (int i = 0; i < atom->getArity() && !result; i++) {
        const Atom* target = atom->getOutgoingAtom(i);
        //printf("Checking atom with TYPE = %s, TV = %s\n", ClassServer::getTypeName(target->getType()), target->getTruthValue().toString().c_str());
        //if (ClassServer::isAssignableFrom(NODE, target->getType())) {
        //printf("Node name = %s\n", ((Node*) target)->getName().c_str());
        //}

        if (target->getTruthValue().getType() == COMPOSITE_TRUTH_VALUE &&
                !((const CompositeTruthValue&) target->getTruthValue()).getVersionedTV(vh).isNullTv()) {
            if (targetType == target->getType()) {
                const char* nodeName = ((Node*) target)->getName().c_str();
                result = !strcmp(targetName, nodeName);
            }
        }
    }
    //printf("matchesFilterCriteria (%s, %s, %s, [%p, %s]) returning %s\n",
    //atom->toString().c_str(),
    //targetName,
    //ClassServer::getTypeName(targetType),
    //vh.substantive,
    //INDICATOR_TYPES[vh.indicator],
    //result?"true":"false");
    return result;
}

HandleEntry* HandleEntry::filterSet(HandleEntry* set, const char* targetName, Type targetType, VersionHandle vh)
{

    HandleEntry* head = set;
    if (!isNullVersionHandle(vh)) {
        HandleEntry* buffer;

        // The search for invalid elements need to be done in two steps because
        // invalid elements found in the middle of the list need to be treated
        // differently from invalid elements found in its begining.

        while ( (set != NULL) && !matchesFilterCriteria(set->getAtom(), targetName, targetType, vh)) {
            buffer = set;
            set = set->next;
            buffer->next = NULL;
            delete buffer;
        }

        if (set == NULL) return NULL;

        head = set;
        while (set->next != NULL) {
            if (!matchesFilterCriteria(set->next->getAtom(), targetName, targetType, vh)) {
                buffer = set->next;
                set->next = set->next->next;
                buffer->next = NULL;
                delete buffer;
            } else {
                set = set->next;
            }
        }
    }

    // head contains the filtered list.
    return head;
}




std::string HandleEntry::toString()
{

    std::string answer;

    for (HandleEntry* current = this; current != NULL; current = current->next) {
        Atom* atom = TLB::getAtom(current->handle);
        if (atom != NULL) {
            if (ClassServer::isAssignableFrom(NODE, atom->getType())) {
                answer += ((Node*) atom)->getName();
            } else if (ClassServer::isAssignableFrom(LINK, atom->getType())) {
                answer += ((Link*) atom)->toShortString();
            }
            //char buf[1024];
            //sprintf(buf,"[table=%d]", atom->getAtomTable());
            //answer += buf;
        }
        answer += " -> ";
    }
    answer += "NULL";
    return answer;
}

Handle* HandleEntry::toHandleVector(Handle*& vector, int& n) throw (InconsistenceException)
{
    n = getSize();
    vector = new Handle[n];
    int i = 0;
    for (HandleEntry* current = this; current != NULL; current = current->next) {
        vector[i++] = current->handle;
    }
    if (i != n) {
        throw RuntimeException(TRACE_INFO, "consistency check failed for handle entry size");
    }
    return vector;
}

std::vector<Handle>& HandleEntry::toHandleVector(std::vector<Handle>& vector)
{
    vector.clear();
    for (HandleEntry* current = this; current != NULL; current = current->next) {
        vector.push_back(current->handle);
    }
    return vector;
}


HandleEntry* HandleEntry::fromHandleVector(Handle* vector, int size)
{
    HandleEntry *ret = NULL;
    for (int i = size - 1; i >= 0; i--) {
        HandleEntry *temp = new HandleEntry(vector[i]);
        ret = HandleEntry::concatenation(temp, ret);
    }
    return(ret);
}
