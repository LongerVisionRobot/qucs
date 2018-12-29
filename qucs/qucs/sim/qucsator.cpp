/***************************************************************************
                             qucsator.cc
                              ----------
    begin                : yes
    copyright            : (C) 2015 by Felix Salfelder
    email                : felix@salfelder.org
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "sim.h"
#include "node.h"
#include <QString>
#include "globals.h"
#include "command.h"

#include "components/subcircuit.h" //  BUG

namespace {
// qucslang language implementation
class QucsLang : public NetLang {
private: // NetLang
  // inline void printItem(Element const* c, stream_t& s) const;

private: // local
  void printCommand(Command const*, QTextStream&) const;
  void printSymbol(Symbol const*, QTextStream&) const;
  void printComponent(Component const*, QTextStream&) const;
};

void QucsLang::printSymbol(Symbol const* d, QTextStream& s) const
{
  if(auto c=dynamic_cast<Component const*>(d)){
    printComponent(c, s);
  }else{
    incomplete();
  }
}

// this does not make sense...
// but it works, because the empty portlist is not visible
void QucsLang::printCommand(Command const* c, QTextStream& s) const
{
  s << ".";

  if(c->isOpen()) {
    // nothing.
  }else if(c->type() == "GND") {
    // nothing.
  }else if(c->isShort()){
    unreachable();
  }else{
    { // todo: introduce proper exceptions
      // normal netlisting

      s << c->type() << ":" << c->label();

      Component const* cc=c;
      qDebug() << "command params" << c->params().count();
      qDebug() << "command params" << cc->params().count();

      for(auto p2 : cc->params()) {
	if(p2->name() != "Symbol") { // hack.
	  s << " " << p2->name() << "=\"" << p2->Value << "\"";
	}
      }

      for(auto p2 : c->params()) {
	if(p2->name() != "Symbol") { // hack.
	  s << " " << p2->name() << "=\"" << p2->Value << "\"";
	}
      }
      s << '\n';
    }
  }
}

/*!
 * print Components in qucs language
 */
void QucsLang::printComponent(Component const* c, QTextStream& s) const
{

  // BUG
  assert(c->isActive == COMP_IS_ACTIVE);

  if(c->isOpen()) {
    // nothing.
  }else if(c->type() == "GND") {
    // nothing.
  }else if(c->isShort()){
    // replace by some resistors (hack?)
    int z=0;
    QListIterator<Port *> iport(c->ports());
    Port *pp = iport.next();
    QString Node1 = pp->Connection->name();
    while (iport.hasNext()){
      s << "R:" << c->label() << "." << QString::number(z++) << " "
	<< Node1 << " " << iport.next()->Connection->name() << " R=\"0\"\n";
    }
//  }else if(Subcircuit const* sub=dynamic_cast<Subcircuit const*>(c)){
//    incomplete();
  }else{
    try{
      // default to the legacy way, fix later
      s << c->getNetlist();
      incomplete();
   }catch (...)
    { // todo: introduce proper exceptions
      // normal netlisting

      s << c->type() << ":" << c->label();

      // output all node names
      for(Port *p1 : c->ports()){
	s << " " << p1->Connection->name();
      }

      for(auto p2 : c->params()) {
	if(p2->name() != "Symbol") { // hack.
	  s << " " << p2->name() << "=\"" << p2->Value << "\"";
	}
      }
      s << '\n';
    }
  }
}

static QucsLang qucslang;
static Dispatcher<NetLang>::INSTALL pl(&netlang_dispatcher, "qucsator", &qucslang);

// qucsator simulator backend
class Qucsator : public Simulator
{
  NetLang const* netLang() const{return &qucslang;}
};

static Qucsator Q;
static Dispatcher<Simulator>::INSTALL p(&simulator_dispatcher, "qucsator", &Q);
}

// vim:ts=8:sw=2:noet