/*
 * ebusd - daemon for communication with eBUS heating systems.
 * Copyright (C) 2014-2017 John Baier <ebusd@ebusd.eu>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIB_EBUS_MESSAGE_H_
#define LIB_EBUS_MESSAGE_H_

#include <stdint.h>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <queue>
#include <functional>
#include "lib/ebus/data.h"
#include "lib/ebus/result.h"
#include "lib/ebus/symbol.h"

namespace ebusd {

/** @file lib/ebus/message.h
 * Classes and functions for decoding and encoding of complete messages on the
 * eBUS to and from readable values.
 *
 * A @a Message has a unique numeric key (see Message#getKey()) as well as a
 * unique name and circuit (see Message#getCircuit() and Message#getName()).
 * The numeric key is built from the message type (active/passive, read/write),
 * the source and destination address, the primary and secondary command byte,
 * as well as additional command ID bytes (see Message#getId()).
 *
 * Whenever a @a Message gets decoded from a master and slave @a SymbolString
 * (see Message#decode()), it stores these strings for later retrieval from
 * cache (see Message#decodeLastData()).
 *
 * In order to make a @a Message available (see Message#isAvailable()) under
 * certain conditions only, it may have assigned a @a Condition instance.
 *
 * A @a Condition is either a @a SimpleCondition referencing another
 * @a Message, numeric field, and field value, or a @a CombinedCondition
 * applying a logical AND on two or more other @a Condition instances.
 *
 * The @a MessageMap stores all @a Message and @a Condition instances by their
 * unique keys, and also keeps track of messages with polling enabled. It reads
 * the instances from configuration files by inheriting the @a FileReader
 * template class.
 */

using std::binary_function;
using std::priority_queue;
using std::deque;

class Condition;
class SimpleCondition;
class CombinedCondition;
class MessageMap;


/**
 * Defines parameters of a message sent or received on the bus.
 */
class Message : public AttributedItem {
  friend class MessageMap;
 public:
  /**
   * Construct a new instance.
   * @param circuit the optional circuit name.
   * @param level the optional access level.
   * @param name the message name (unique within the same circuit and type).
   * @param isWrite whether this is a write message.
   * @param isPassive true if message can only be initiated by a participant other than us,
   * false if message can be initiated by any participant.
   * @param attributes the additional named attributes.
   * @param srcAddress the source address, or @a SYN for any (only relevant if passive).
   * @param dstAddress the destination address, or @a SYN for any (set later).
   * @param id the primary, secondary, and optional further ID bytes.
   * @param data the @a DataField for encoding/decoding the message.
   * @param deleteData whether to delete the @a DataField during destruction.
   * @param pollPriority the priority for polling, or 0 for no polling at all.
   * @param condition the @a Condition for this message, or NULL.
   */
  Message(const string circuit, const string level, const string name,
      const bool isWrite, const bool isPassive, const map<string, string>& attributes,
      const symbol_t srcAddress, const symbol_t dstAddress,
      const vector<symbol_t> id,
      const DataField* data, const bool deleteData,
      const size_t pollPriority = 0,
      Condition* condition = NULL);


 private:
  /**
   * Construct a new scan @a Message instance.
   * @param circuit the circuit name, or empty for not storing by name.
   * @param level the optional access level.
   * @param name the message name (unique within the same circuit and type), or empty for not storing by name.
   * @param pb the primary ID byte.
   * @param sb the secondary ID byte.
   * @param broadcast true for broadcast scan message, false for scan message to be sent to a slave address.
   * @param data the @a DataField for encoding/decoding the message.
   * @param deleteData whether to delete the @a DataField during destruction.
   */
  Message(const string circuit, const string level, const string name,
      const symbol_t pb, const symbol_t sb,
      const bool broadcast, const DataField* data, const bool deleteData);


 public:
  /**
   * Destructor.
   */
  virtual ~Message() { if (m_deleteData) { delete m_data; } }

  /**
   * Calculate the key for the ID.
   * @param id the primary, secondary, and optional further ID bytes.
   * @param isWrite whether this is a write message.
   * @param isPassive true if message can only be initiated by a participant other than us,
   * false if message can be initiated by any participant.
   * @param srcAddress the source address, or @a SYN for any (only relevant if passive).
   * @param dstAddress the destination address, or @a SYN for any (set later).
   * @return the key for the ID.
   */
  static uint64_t createKey(const vector<symbol_t> id,
    const bool isWrite, const bool isPassive,
    const symbol_t srcAddress, const symbol_t dstAddress);

  /**
   * Calculate the key for the @a MasterSymbolString.
   * @param master the @a MasterSymbolString.
   * @param maxIdLength the maximum ID length to use
   * @param anyDestination @p true to use the special @a SYN as destination address in the key.
   * @return the key for the ID, or -1LL if the data is invalid.
   */
  static uint64_t createKey(const MasterSymbolString& master, size_t maxIdLength, bool anyDestination = false);

  /**
   * Calculate the key for a scan message.
   * @param pb the primary ID byte.
   * @param sb the secondary ID byte.
   * @param broadcast true for broadcast scan message, false for scan message to be sent to a slave address.
   * @return the key for the scan message.
   */
  static uint64_t createKey(const symbol_t pb, const symbol_t sb, const bool broadcast);

  /**
   * Get the length field from the key.
   * @param key the key.
   * @return the length field from the key.
   */
  static size_t getKeyLength(uint64_t key) { return (size_t)(key >> (8 * 7 + 5)); }

  /**
   * Parse an ID part from the input @a string.
   * @param input the input @a string, hex digits optionally separated by space.
   * @param id the vector to which to add the parsed values.
   * @return @a RESULT_OK on success, or an error code.
   */
  static result_t parseId(string input, vector<symbol_t>& id);

  /**
   * Factory method for creating new instances.
   * @param row the mapped message definition row.
   * @param subRows the mapped field definition rows.
   * @param rowDefaults the mapped message definition defaults.
   * @param subRowDefaults the mapped field definition defaults.
   * @param errorDescription a string in which to store the error description in case of error.
   * @param condition the @a Condition instance for the message, or NULL.
   * @param filename the name of the file being read.
   * @param templates the @a DataFieldTemplates to be referenced by name, or NULL.
   * @param messages the @a vector to which to add created instances.
   * @return @a RESULT_OK on success, or an error code.
   * Note: the caller needs to free the created instances.
   */
  static result_t create(map<string, string> row, vector< map<string, string> > subRows,
      map<string, map<string, string> >& rowDefaults, map<string, vector< map<string, string> > >& subRowDefaults,
      string& errorDescription, Condition* condition, const string filename, DataFieldTemplates* templates,
      vector<Message*>& messages);

  /**
   * Create a new scan @a Message instance.
   * @param broadcast true for broadcast scan message, false for scan message to be sent to a slave address.
   * @return the new scan @a Message instance.
   */
  static Message* createScanMessage(bool broadcast = false);

  /**
   * Extract the known field names from the input string.
   * @param str the input string with the field names separated by @a FIELD_SEPARATOR.
   * @param fields the vector to update with the extracted normalized field names with.
   * @param checkAbbreviated true to also check for abbreviated field names.
   * @return true when all fields are valid.
   */
  static bool extractFieldNames(string str, vector<string>& fields, bool checkAbbreviated = true);

  /**
   * Set that this is a special scanning @a Message instance.
   */
  void setScanMessage() { m_isScanMessage = true; }

  /**
   * Return whether this is a special scanning @a Message instance.
   * @return whether this is a special scanning @a Message instance.
   */
  bool isScanMessage() const { return m_isScanMessage; }

  /**
   * Derive a new @a Message from this message.
   * @param dstAddress the new destination address.
   * @param srcAddress the new source address, or @a SYN to keep the current source address.
   * @param circuit the new circuit name, or empty to use the current circuit name.
   * @return the derived @a Message instance.
   */
  virtual Message* derive(const symbol_t dstAddress, const symbol_t srcAddress = SYN,
      const string circuit = "") const;

  /**
   * Derive a new @a Message from this message.
   * @param dstAddress the new destination address.
   * @param extendCircuit whether to extend the current circuit name with a dot and the new destination address in hex.
   * @return the derived @a ScanMessage instance.
   */
  Message* derive(const symbol_t dstAddress, const bool extendCircuit) const;

  /**
   * Get the optional circuit name.
   * @return the optional circuit name.
   */
  string getCircuit() const { return m_circuit; }

  /**
   * Get the optional access level.
   * @return the optional access level.
   */
  string getLevel() const { return m_level; }

  /**
   * Return whether one of the specified access levels allows access to this message.
   * @param levels the allowed access levels to check, separated by semicolon.
   * @param includeEmpty true to also allow this message when the message level is empty but does not match the
   * level to check.
   * @return true when access is granted.
   */
  bool hasLevel(const string levels, bool includeEmpty = true) const {
    return m_level.empty() ? (includeEmpty || levels.empty()) : checkLevel(m_level, levels);
  }

  /**
   * Check if the access level is part of the levels.
   * @param level the access level to check.
   * @param checkLevels the access levels to check against, separated by semicolon.
   * @return whether the access level matches.
   */
  static bool checkLevel(const string level, const string checkLevels);

  /**
   * Get the specified field name.
   * @param fieldIndex the index of the field.
   * @return the field name, or the index as string if not unique or not available.
   */
  virtual string getFieldName(const ssize_t fieldIndex) const { return m_data->getName(fieldIndex); }

  /**
   * Get whether this is a write message.
   * @return whether this is a write message.
   */
  bool isWrite() const { return m_isWrite; }

  /**
   * Get whether message can be initiated only by a participant other than us.
   * @return true if message can only be initiated by a participant other than us,
   * false if message can be initiated by any participant.
   */
  bool isPassive() const { return m_isPassive; }

  /**
   * Get the source address.
   * @return the source address, or @a SYN for any.
   */
  symbol_t getSrcAddress() const { return m_srcAddress; }

  /**
   * Get the destination address.
   * @return the destination address, or @a SYN for any.
   */
  symbol_t getDstAddress() const { return m_dstAddress; }

  /**
   * Get the primary command byte.
   * @return the primary command byte.
   */
  symbol_t getPrimaryCommand() const { return m_id[0]; }

  /**
   * Get the secondary command byte.
   * @return the secondary command byte.
   */
  symbol_t getSecondaryCommand() const { return m_id[1]; }

  /**
   * Get the length of the ID bytes (without primary and secondary command bytes).
   * @return the length of the ID bytes (without primary and secondary command bytes).
   */
  virtual size_t getIdLength() const { return m_id.size() - 2; }

  /**
   * Check if the full command ID starts with the given value.
   * @param id the ID bytes to check against.
   * @return true if the full command ID starts with the given value.
   */
  bool checkIdPrefix(const vector<symbol_t>& id) const;

  /**
   * Check the ID against the master @a SymbolString data.
   * @param master the @a MasterSymbolString to check against.
   * @param index the variable in which to store the message part index, or NULL to ignore.
   * @return true if the ID matches, false otherwise.
   */
  virtual bool checkId(const MasterSymbolString& master, size_t* index = NULL) const;

  /**
   * Check the ID against the other @a Message.
   * @param other the other @a Message to check against.
   * @return true if the ID matches, false otherwise.
   */
  virtual bool checkId(Message& other) const;

  /**
   * Return the key for storing in @a MessageMap.
   * @return the key for storing in @a MessageMap.
   */
  uint64_t getKey() const { return m_key; }

  /**
   * Return the derived key for storing in @a MessageMap.
   * @param dstAddress the destination address for the derivation.
   * @return the derived key for storing in @a MessageMap.
   */
  uint64_t getDerivedKey(const symbol_t dstAddress) const;

  /**
   * Get the polling priority, or 0 for no polling at all.
   * @return the polling priority, or 0 for no polling at all.
   */
  size_t getPollPriority() const { return m_pollPriority; }

  /**
   * Set the polling priority.
   * @param priority the polling priority, or 0 for no polling at all.
   * @return true when the priority was changed and polling was not enabled before, false otherwise.
   */
  bool setPollPriority(size_t priority);

  /**
   * Set the poll priority suitable for resolving a @a Condition.
   */
  void setUsedByCondition();

  /**
   * Return whether this @a Message depends on a @a Condition.
   * @return true when this @a Message depends on a @a Condition.
   */
  bool isConditional() const { return m_condition != NULL; }

  /**
   * Return whether this @a Message is available (optionally depending on a @a Condition evaluation).
   * @return true when this @a Message is available.
   */
  bool isAvailable();

  /**
   * Return whether the field is available.
   * @param fieldName the name of the field to find, or NULL for any.
   * @param numeric true for a numeric field, false for a string field.
   * @return true if the field is available.
   */
  bool hasField(const char* fieldName, bool numeric = true) const;

  /**
   * @return the number of parts this message is composed of.
   */
  virtual size_t getCount() const { return 1; }

  /**
   * Prepare the master @a SymbolString for sending a query or command to the bus.
   * @param srcAddress the source address to set.
   * @param master the @a MasterSymbolString for writing symbols to.
   * @param input the @a istringstream to parse the formatted value(s) from.
   * @param separator the separator character between multiple fields.
   * @param dstAddress the destination address to set, or @a SYN to keep the address defined during construction.
   * @param index the index of the part to prepare.
   * @return @a RESULT_OK on success, or an error code.
   */
  result_t prepareMaster(const symbol_t srcAddress, MasterSymbolString& master,
      istringstream& input, char separator = UI_FIELD_SEPARATOR,
      const symbol_t dstAddress = SYN, size_t index = 0);


 protected:
  /**
   * Prepare a part of the master data @a SymbolString for sending (everything including NN).
   * @param master the @a MasterSymbolString for writing symbols to.
   * @param input the @a istringstream to parse the formatted value(s) from.
   * @param separator the separator character between multiple fields.
   * @param index the index of the part to prepare.
   * @return @a RESULT_OK on success, or an error code.
   */
  virtual result_t prepareMasterPart(MasterSymbolString& master, istringstream& input, char separator,
      size_t index);


 public:
  /**
   * Prepare the slave @a SymbolString for sending an answer to the bus.
   * @param input the @a istringstream to parse the formatted value(s) from.
   * @param slave the @a SlaveSymbolString for writing symbols to.
   * @return @a RESULT_OK on success, or an error code.
   */
  virtual result_t prepareSlave(istringstream& input, SlaveSymbolString& slave);

  /**
   * Store the last seen master and slave data.
   * @param master the last seen @a MasterSymbolString.
   * @param slave the last seen @a SlaveSymbolString.
   * @return @a RESULT_OK on success, or an error code.
   */
  virtual result_t storeLastData(MasterSymbolString& master, SlaveSymbolString& slave);

  /**
   * Store last seen master data.
   * @param data the last @a MasterSymbolString.
   * @param index the index of the part to store.
   * @return @a RESULT_OK on success, or an error code.
   */
  virtual result_t storeLastData(MasterSymbolString& data, size_t index);

  /**
   * Store last seen slave data.
   * @param data the last seen @a SlaveSymbolString.
   * @param index the index of the part to store.
   * @return @a RESULT_OK on success, or an error code.
   */
  virtual result_t storeLastData(SlaveSymbolString& data, size_t index);

  /**
   * Decode the value from the last stored master data.
   * @param output the @a ostringstream to append the formatted value to.
   * @param outputFormat the @a OutputFormat options to use.
   * @param leadingSeparator whether to prepend a separator before the formatted value.
   * @param fieldName the optional name of a field to limit the output to.
   * @param fieldIndex the optional index of the named field to limit the output to, or -1.
   * @return @a RESULT_OK on success, or an error code.
   */
  virtual result_t decodeLastMasterData(ostringstream& output, OutputFormat outputFormat = 0,
      bool leadingSeparator = false, const char* fieldName = NULL, ssize_t fieldIndex = -1) const;

  /**
   * Decode the value from the last stored slave data.
   * @param output the @a ostringstream to append the formatted value to.
   * @param outputFormat the @a OutputFormat options to use.
   * @param leadingSeparator whether to prepend a separator before the formatted value.
   * @param fieldName the optional name of a field to limit the output to.
   * @param fieldIndex the optional index of the named field to limit the output to, or -1.
   * @return @a RESULT_OK on success, or an error code.
   */
  virtual result_t decodeLastSlaveData(ostringstream& output, OutputFormat outputFormat = 0,
      bool leadingSeparator = false, const char* fieldName = NULL, ssize_t fieldIndex = -1) const;

  /**
   * Decode the value from the last stored data.
   * @param output the @a ostringstream to append the formatted value to.
   * @param outputFormat the @a OutputFormat options to use.
   * @param leadingSeparator whether to prepend a separator before the formatted value.
   * @param fieldName the optional name of a field to limit the output to.
   * @param fieldIndex the optional index of the named field to limit the output to, or -1.
   * @return @a RESULT_OK on success, or an error code.
   */
  virtual result_t decodeLastData(ostringstream& output, OutputFormat outputFormat = 0,
      bool leadingSeparator = false, const char* fieldName = NULL, ssize_t fieldIndex = -1) const;

  /**
   * Decode a particular numeric field value from the last stored data.
   * @param output the variable in which to store the value.
   * @param fieldName the name of the field to decode, or NULL for the first field.
   * @param fieldIndex the optional index of the named field, or -1.
   * @return @a RESULT_OK on success, or an error code.
   */
  virtual result_t decodeLastDataNumField(unsigned int& output, const char* fieldName, ssize_t fieldIndex = -1) const;

  /**
   * Get the last seen master data.
   * @return the last seen @a MasterSymbolString.
   */
  const MasterSymbolString& getLastMasterData() const { return m_lastMasterData; }

  /**
   * Get the last seen slave data.
   * @return the last seen @a SlaveSymbolString.
   */
  const SlaveSymbolString& getLastSlaveData() const { return m_lastSlaveData; }

  /**
   * Get the time when this message was last seen with reasonable data.
   * @return the time when this message was last seen, or 0.
   */
  time_t getLastUpdateTime() const { return m_lastUpdateTime; }

  /**
   * Get the time when the message data was last changed.
   * @return the time when the message data was last changed, or 0 if this message was not decoded yet.
   */
  time_t getLastChangeTime() const { return m_lastChangeTime; }

  /**
   * Get the time when this message was last polled for.
   * @return the time when this message was last polled for, or 0 for never.
   */
  time_t getLastPollTime() const { return m_lastPollTime; }

  /**
   * Return whether this @a Message needs to be polled after the other one.
   * @param other the other @a Message to compare with.
   * @return true if this @a Message needs to be polled after the other one.
   */
  bool isLessPollWeight(const Message* other) const;

  /**
   * Write the message definition header or parts of it to the @a ostream.
   * @param output the @a ostream to append the formatted value to.
   * @param fieldNames the list of field names to write, or NULL for all.
   */
  static void dumpHeader(ostream& output, vector<string>* fieldNames = NULL);

  /**
   * Write the message definition or parts of it to the @a ostream.
   * @param output the @a ostream to append the formatted value to.
   * @param fieldNames the list of field names to write, or NULL for all.
   * @param withConditions whether to include the optional conditions prefix.
   */
  void dump(ostream& output, vector<string>* fieldNames = NULL, bool withConditions = false) const;

  /**
   * Write the specified field to the @a ostream.
   * @param output the @a ostream to append the formatted value to.
   * @param fieldName the field name to write.
   * @param withConditions whether to include the optional conditions prefix.
   */
  virtual void dumpField(ostream& output, string fieldName, bool withConditions = false) const;

  /**
   * Decode the message from the last stored data.
   * @param output the @a ostringstream to append the decoded value(s) to.
   * @param outputFormat the @a OutputFormat options to use.
   * @param leadingSeparator whether to prepend a separator before the first value.
   * @param fields the list of message and/or data field fields to write, or NULL for all.
   */
  virtual void decode(ostringstream& output, OutputFormat outputFormat = 0, bool leadingSeparator = false,
      vector<string>* fields = NULL) const;

 protected:
  /** the optional circuit name. */
  const string m_circuit;

  /** the optional access level. */
  const string m_level;

  /** whether this is a write message. */
  const bool m_isWrite;

  /** true if message can only be initiated by a participant other than us,
   * false if message can be initiated by any participant. */
  const bool m_isPassive;

  /** the source address, or @a SYN for any (only relevant if passive). */
  const symbol_t m_srcAddress;

  /** the destination address, or @a SYN for any (only for temporary scan messages). */
  const symbol_t m_dstAddress;

  /** the primary, secondary, and optionally further command ID bytes. */
  const vector<symbol_t> m_id;

  /**
   * the key for storing in @a MessageMap.
   * <ul>
   * <li>byte 7:
   *  <ul>
   *   <li>bits 5-7: length of ID bytes (without PB/SB)</li>
   *   <li>bits 0-4:
   *    <ul>
   *     <li>master number (1..25) of sender for passive message</li>
   *     <li>0x00 for passive message with any sender</li>
   *     <li>0x1f for active write</li>
   *     <li>0x1e for active read</li>
   *    </ul>
   *  </ul>
   * </li>
   * <li>byte 6: ZZ or SYN for any</li>
   * <li>byte 5: PB</li>
   * <li>byte 4: SB</li>
   * <li>bytes 3-0: ID bytes (with cyclic xor if more than 4)</li>
   * </ul>
   */
  const uint64_t m_key;

  /** the @a DataField for encoding/decoding the message. */
  const DataField* m_data;

  /** whether to delete the @a DataField during destruction. */
  const bool m_deleteData;

  /** the priority for polling, or 0 for no polling at all. */
  size_t m_pollPriority;

  /** whether this message is used by a @a Condition. */
  bool m_usedByCondition;

  /** whether this is a special scanning @a Message instance. */
  bool m_isScanMessage;

  /** the @a Condition for this message, or NULL. */
  Condition* m_condition;

  /** the last seen @a MasterSymbolString. */
  MasterSymbolString m_lastMasterData;

  /** the last seen @a SlaveSymbolString. */
  SlaveSymbolString m_lastSlaveData;

  /** the system time when the message was last updated, 0 for never. */
  time_t m_lastUpdateTime;

  /** the system time when the message content was last changed, 0 for never. */
  time_t m_lastChangeTime;

  /** the number of times this messages was already polled for. */
  unsigned int m_pollCount;

  /** the system time when this message was last polled for, 0 for never. */
  time_t m_lastPollTime;
};


/**
 * A chained @a Message that needs more than one read/write on the bus to collect/send the data.
 */
class ChainedMessage : public Message {
 public:
  /**
   * Construct a new instance.
   * @param circuit the optional circuit name.
   * @param level the optional access level.
   * @param name the message name (unique within the same circuit and type).
   * @param isWrite whether this is a write message.
   * @param attributes the additional named attributes.
   * @param srcAddress the source address, or @a SYN for any (only relevant if passive).
   * @param dstAddress the destination address, or @a SYN for any (set later).
   * @param id the primary, secondary, and optional further ID bytes common to each part of the chain.
   * @param ids the primary, secondary, and optional further ID bytes for each part of the chain.
   * @param lengths the data length for each part of the chain.
   * @param data the @a DataField for encoding/decoding the chained message.
   * @param deleteData whether to delete the @a DataField during destruction.
   * @param pollPriority the priority for polling, or 0 for no polling at all.
   * @param condition the @a Condition for this message, or NULL.
   */
  ChainedMessage(const string circuit, const string level, const string name,
      const bool isWrite, const map<string, string>& attributes,
      const symbol_t srcAddress, const symbol_t dstAddress,
      const vector<symbol_t> id,
      vector< vector<symbol_t> > ids, vector<size_t> lengths,
      const DataField* data, const bool deleteData,
      const size_t pollPriority,
      Condition* condition = NULL);

  virtual ~ChainedMessage();

  // @copydoc
  Message* derive(const symbol_t dstAddress, const symbol_t srcAddress = SYN,
      const string circuit = "") const override;

  // @copydoc
  size_t getIdLength() const override { return m_ids[0].size() - 2; }

  // @copydoc
  bool checkId(const MasterSymbolString& master, size_t* index = NULL) const override;

  // @copydoc
  bool checkId(Message& other) const override;

  // @copydoc
  size_t getCount() const override { return m_ids.size(); }


 protected:
  // @copydoc
  result_t prepareMasterPart(MasterSymbolString& master, istringstream& input, char separator,
      size_t index) override;


 public:
  // @copydoc
  result_t storeLastData(MasterSymbolString& master, SlaveSymbolString& slave) override;

  // @copydoc
  result_t storeLastData(MasterSymbolString& data, size_t index) override;

  // @copydoc
  result_t storeLastData(SlaveSymbolString& data, size_t index) override;

  /**
   * Combine all last stored data.
   * @return the result code.
   */
  virtual result_t combineLastParts();

 protected:
  // @copydoc
  void dumpField(ostream& output, string fieldName, bool withConditions = false) const override;


 private:
  /** the primary, secondary, and optional further ID bytes for each part of the chain. */
  const vector< vector<symbol_t> > m_ids;

  /** the data length for each part of the chain. */
  const vector<size_t> m_lengths;

  /** the maximum allowed time difference of any data pair. */
  const time_t m_maxTimeDiff;

  /** array of the last seen @a MasterSymbolString instances. */
  MasterSymbolString** m_lastMasterDatas;

  /** array of the last seen @a SlaveSymbolString instances. */
  SlaveSymbolString** m_lastSlaveDatas;

  /** array of the system times when the corresponding master data was last updated, 0 for never. */
  time_t* m_lastMasterUpdateTimes;

  /** array of the system times when the corresponding slave data was last updated, 0 for never. */
  time_t* m_lastSlaveUpdateTimes;
};


/**
 * A function that compares the weighted poll priority of two @a Message instances.
 */
struct compareMessagePriority : binary_function<Message*, Message*, bool> {
  /**
   * Compare the weighted poll priority of the two @a Message instances.
   * @param x the first @a Message.
   * @param y the second @a Message.
   * @return whether @a x is smaller than @a y with regard to their weighted poll priority.
   */
  bool operator() (Message* x, Message* y) const { return x->isLessPollWeight(y); }
};


/**
 * Helper class extending @a priority_queue to hold distinct values only.
 */
class MessagePriorityQueue
  : public priority_queue<Message*, vector<Message*>, compareMessagePriority> {
 public:
  /**
   * Add data to the queue and ensure it is contained only once.
   * @param __x the element to add.
   */
  void push(const value_type& __x) {
    for (vector<Message*>::iterator it = c.begin(); it != c.end(); it++) {
      if (*it == __x) {
        c.erase(it);
        break;
      }
    }
    priority_queue<Message*, vector<Message*>, compareMessagePriority>::push(__x);
  }
};


/**
 * An abstract condition based on the value of one or more @a Message instances.
 */
class Condition {
 public:
  /**
   * Construct a new instance.
   */
  Condition()
    : m_lastCheckTime(0), m_isTrue(false) { }

  /**
   * Destructor.
   */
  virtual ~Condition() { }

  /**
   * Factory method for creating a new instance.
   * @param condName the name of the condition.
   * @param row the mapped definition row.
   * @param rowDefaults the mapped definition defaults.
   * @param returnValue the variable in which to store the created instance.
   * @return @a RESULT_OK on success, or an error code.
   */
  static result_t create(const string condName, map<string, string> row, map<string, string> rowDefaults,
      SimpleCondition*& returnValue);

  /**
   * Derive a new @a SimpleCondition from this condition.
   * @param valueList the @a string with the new list of values.
   * @return the derived @a SimpleCondition instance, or NULL if the value list is invalid.
   */
  virtual SimpleCondition* derive(string valueList) const { return NULL; }

  /**
   * Write the condition definition or resolved expression to the @a ostream.
   * @param output the @a ostream to append to.
   * @param matched true for dumping the matched value if the condition is true, false for dumping the definition.
   */
  virtual void dump(ostream& output, bool matched = false) const = 0;

  /**
   * Combine this condition with another instance using a logical and.
   * @param other the @a Condition to combine with.
   * @return the @a CombinedCondition instance.
   */
  virtual CombinedCondition* combineAnd(Condition* other) = 0;

  /**
   * Resolve the referred @a Message instance(s) and field index(es).
   * @param messages the @a MessageMap instance for resolving.
   * @param errorMessage a @a ostringstream to which to add optional error messages.
   * @param readMessageFunc the function to call for immediate reading of a @a Message from the bus, or NULL.
   * @return @a RESULT_OK on success, or an error code.
   */
  virtual result_t resolve(MessageMap* messages, ostringstream& errorMessage,
      void (*readMessageFunc)(Message* message) = NULL) = 0;

  /**
   * Check and return whether this condition is fulfilled.
   * @return whether this condition is fulfilled.
   */
  virtual bool isTrue() = 0;


 protected:
  /** the system time when the condition was last checked, 0 for never. */
  time_t m_lastCheckTime;

  /** whether the condition was @a true during the last check. */
  bool m_isTrue;
};


/**
 * A simple @a Condition based on the value of one @a Message.
 */
class SimpleCondition : public Condition {
 public:
  /**
   * Construct a new instance.
   * @param condName the name of the condition.
   * @param refName the reference name for dumping.
   * @param circuit the circuit name.
   * @param level the access level.
   * @param name the message name, or empty for scan message.
   * @param dstAddress the override destination address, or @a SYN (only for @a Message without specific destination
   * as well as scan message).
   * @param field the field name.
   * @param hasValues whether a value has to be checked against.
   */
  SimpleCondition(const string condName, const string refName, const string circuit, const string level,
      const string name, const symbol_t dstAddress, const string field, const bool hasValues = false)
    : Condition(),
      m_condName(condName), m_refName(refName), m_circuit(circuit), m_level(level), m_name(name),
      m_dstAddress(dstAddress), m_field(field), m_hasValues(hasValues), m_message(NULL) { }

  /**
   * Destructor.
   */
  virtual ~SimpleCondition() {}

  // @copydoc
  SimpleCondition* derive(string valueList) const override;

  // @copydoc
  void dump(ostream& output, bool matched = false) const override;

  // @copydoc
  CombinedCondition* combineAnd(Condition* other) override;

  // @copydoc
  result_t resolve(MessageMap* messages, ostringstream& errorMessage,
      void (*readMessageFunc)(Message* message) = NULL) override;

  // @copydoc
  bool isTrue() override;

  /**
   * Return whether the condition is based on a numeric value.
   * @return whether the condition is based on a numeric value.
   */
  virtual bool isNumeric() const { return true; }


 protected:
  /**
   * Check the values against the field in the @a Message.
   * @param message the @a Message to check against.
   * @param field the field name to check against, or empty for first field.
   * @return whether the field matches one of the valid values.
   */
  virtual bool checkValue(Message* message, const string field) { return true; }

  /** the value that matched in @a checkValue. */
  string m_matchedValue;


 private:
  /** the condition name. */
  const string m_condName;

  /** the reference name for dumping. */
  const string m_refName;

  /** the circuit name. */
  const string m_circuit;

  /** the access level. */
  const string m_level;

  /** the message name, or empty for scan message. */
  const string m_name;

  /** the override destination address, or @a SYN (only for @a Message without specific destination as well as scan
   * message). */
  const symbol_t m_dstAddress;

  /** the field name, or empty for first field. */
  const string m_field;

  /** whether a value has to be checked against. */
  const bool m_hasValues;

  /** the resolved @a Message instance, or NULL. */
  Message* m_message;
};


/**
 * A simple @a Condition based on the numeric value of one @a Message.
 */
class SimpleNumericCondition : public SimpleCondition {
 public:
  /**
   * Construct a new instance.
   * @param condName the name of the condition.
   * @param refName the reference name for dumping.
   * @param circuit the circuit name.
   * @param level the access level.
   * @param name the message name, or empty for scan message.
   * @param dstAddress the override destination address, or @a SYN (only for @a Message without specific destination as well as scan message).
   * @param field the field name.
   * @param valueRanges the valid value ranges (pairs of from/to inclusive), empty for @a m_message seen check.
   */
  SimpleNumericCondition(const string condName, const string refName, const string circuit, const string level,
      const string name, const symbol_t dstAddress, const string field, const vector<unsigned int> valueRanges)
    : SimpleCondition(condName, refName, circuit, level, name, dstAddress, field, true),
      m_valueRanges(valueRanges) { }

  /**
   * Destructor.
   */
  virtual ~SimpleNumericCondition() {}


 protected:
  // @copydoc
  bool checkValue(Message* message, const string field) override;


 private:
  /** the valid value ranges (pairs of from/to inclusive), empty for @a m_message seen check. */
  const vector<unsigned int> m_valueRanges;
};


/**
 * A simple @a Condition based on the string value of one @a Message.
 */
class SimpleStringCondition : public SimpleCondition {
 public:
  /**
   * Construct a new instance.
   * @param condName the name of the condition.
   * @param refName the reference name for dumping.
   * @param circuit the circuit name.
   * @param level the access level.
   * @param name the message name, or empty for scan message.
   * @param dstAddress the override destination address, or @a SYN (only for @a Message without specific destination as well as scan message).
   * @param field the field name.
   * @param values the valid values.
   */
  SimpleStringCondition(const string condName, const string refName, const string circuit, const string level,
      const string name, const symbol_t dstAddress, const string field, const vector<string> values)
    : SimpleCondition(condName, refName, circuit, level, name, dstAddress, field, true),
      m_values(values) { }

  /**
   * Destructor.
   */
  virtual ~SimpleStringCondition() {}

  // @copydoc
  bool isNumeric() const override { return false; }


 protected:
  // @copydoc
  bool checkValue(Message* message, const string field) override;


 private:
  /** the valid values. */
  const vector<string> m_values;
};


/**
 * A @a Condition combining two or more @a SimpleCondition instances with a logical and.
 */
class CombinedCondition : public Condition {
 public:
  /**
   * Construct a new instance.
   */
  CombinedCondition()
    : Condition() { }

  /**
   * Destructor.
   */
  virtual ~CombinedCondition() {}

  // @copydoc
  void dump(ostream& output, bool matched = false) const override;

  // @copydoc
  CombinedCondition* combineAnd(Condition* other) override { m_conditions.push_back(other); return this; }

  // @copydoc
  result_t resolve(MessageMap* messages, ostringstream& errorMessage,
      void (*readMessageFunc)(Message* message) = NULL) override;

  // @copydoc
  bool isTrue() override;


 private:
  /** the @a Condition instances used. */
  vector<Condition*> m_conditions;
};


/**
 * An abstract instruction based on the value of one or more @a Message instances.
 */
class Instruction {
 public:
  /**
   * Construct a new instance.
   * @param condition the @a Condition this instruction requires, or null.
   * @param singleton whether this @a Instruction belongs to a set of instructions of which only the first one may be
   * executed for the same source file.
   * @param defaults the mapped definition defaults.
   */
  Instruction(Condition* condition, const bool singleton, const map<string, string>& defaults)
    : m_condition(condition), m_singleton(singleton), m_defaults(defaults) { }

  /**
   * Destructor.
   */
  virtual ~Instruction() { }

  /**
   * Factory method for creating a new instance.
   * @param contextPath the path and/or filename context being loaded.
   * @param type the type of the instruction.
   * @param condition the @a Condition for the instruction, or NULL.
   * @param row the definition row by field name.
   * @param defaults the default values by name.
   * @param returnValue the variable in which to store the created instance.
   * @return @a RESULT_OK on success, or an error code.
   */
  static result_t create(const string& contextPath, const string type,
      Condition* condition, map<string, string>& row, map<string, string>& defaults,
      Instruction*& returnValue);

  /**
   * Return the @a Condition this instruction requires.
   * @return the @a Condition this instruction requires, or null.
   */
  Condition* getCondition() { return m_condition; }

  /**
   * Return whether this @a Instruction belongs to a set of instructions of which only the first one may be executed
   * for the same source file.
   * @return whether this @a Instruction belongs to a set of instructions of which only the first one may be executed
   * for the same source file.
   */
  bool isSingleton() const { return m_singleton; }

  /**
   * Return a string describing the destination from the stored default values.
   * @return a string describing the destination.
   */
  string getDestination() const;

  /**
   * Execute the instruction.
   * @param messages the @a MessageMap.
   * @param log the @a ostringstream to log success messages to (if necessary).
   * @param condition the @a Condition that was successfully evaluated for execution, or NULL.
   * @return @a RESULT_OK on success, or an error code.
   */
  virtual result_t execute(MessageMap* messages, ostringstream& log, Condition* condition) = 0;


 private:
  /** the @a Condition this instruction requires, or null. */
  Condition* m_condition;

  /** whether this @a Instruction belongs to a set of instructions of which only the first one may be executed for the
   * same source file. */
  const bool m_singleton;


 protected:
  /** the defaults by field name. */
  map<string, string> m_defaults;
};


/**
 * An @a Instruction allowing to load another file.
 */
class LoadInstruction : public Instruction {
 public:
  /**
   * Construct a new instance.
   * @param condition the @a Condition this instruction requires, or null.
   * @param singleton whether this @a Instruction belongs to a set of instructions of which only the first one may be
   * executed for the same source file.
   * @param defaults the mapped definition defaults.
   * @param filename the name of the file to load.
   */
  LoadInstruction(Condition* condition, const bool singleton, map<string, string>& defaults, const string filename)
    : Instruction(condition, singleton, defaults), m_filename(filename) { }

  /**
   * Destructor.
   */
  virtual ~LoadInstruction() { }

  // @copydoc
  result_t execute(MessageMap* messages, ostringstream& log, Condition* condition) override;


 private:
  /** the name of the file to load. */
  const string m_filename;
};


/**
 * Helper class for information about a loaded file.
 */
class LoadedFileInfo {
 public:
  /** the optional comment for the file. */
  string m_comment;

  /** the hash of the file. */
  size_t m_hash;

  /** the normalized size of the file. */
  size_t m_size;

  /** the modification time of the file. */
  time_t m_time;
};


/**
 * Holds a map of all known @a Message instances.
 */
class MessageMap : public MappedFileReader {
 public:
  /**
   * Construct a new instance.
   * @param configPath the path to the configuration files.
   * @param addAll whether to add all messages, even if duplicate.
   * @param preferLanguage the preferred language to use, or empty.
   */
  explicit MessageMap(const string configPath, const bool addAll = false, const string preferLanguage = "")
  : MappedFileReader::MappedFileReader(true),
    m_configPath(configPath),
    m_addAll(addAll), m_additionalScanMessages(false), m_maxIdLength(0), m_maxBroadcastIdLength(0),
    m_messageCount(0), m_conditionalMessageCount(0), m_passiveMessageCount(0) {
    m_scanMessage = Message::createScanMessage();
    m_broadcastScanMessage = Message::createScanMessage(true);
  }

  /**
   * Destructor.
   */
  virtual ~MessageMap() {
    clear();
    if (m_scanMessage) {
      delete m_scanMessage;
      m_scanMessage = NULL;
    }
    if (m_broadcastScanMessage) {
      delete m_broadcastScanMessage;
      m_broadcastScanMessage = NULL;
    }
  }

  /**
   * Return the relative file name of the given filename.
   * @param filename the name of the configuration file (including relative path).
   * @return the relative file name.
   */
  const string getRelativePath(const string filename) const;

  /**
   * Add a @a Message instance to this set.
   * @param message the @a Message instance to add.
   * @param storeByName whether to store the @a Message by name.
   * @return @a RESULT_OK on success, or an error code.
   * Note: the caller may not free the added instance on success.
   */
  result_t add(Message* message, bool storeByName = true);

  // @copydoc
  result_t getFieldMap(vector<string>& row, string& errorDescription, const string preferLanguage) const override;

  // @copydoc
  result_t addDefaultFromFile(map<string, string>& row, vector< map<string, string> >& subRows,
      string& errorDescription, const string filename, unsigned int lineNo) override;

  /**
   * Read the @a Condition instance(s) from the types field.
   * @param types the field from which to read the @a Condition instance(s).
   * @param filename the name of the file being read.
   * @param errorDescription a string in which to store the error description in case of error.
   * @param condition the variable in which to store the result.
   * @return @a RESULT_OK on success, or an error code.
   */
  result_t readConditions(string& types, const string filename, string& errorDescription, Condition*& condition);

  // @copydoc
  bool extractDefaultsFromFilename(string filename, map<string, string>& defaults,
      symbol_t* destAddress = NULL, unsigned int* software = NULL, unsigned int* hardware = NULL) const override;

  // @copydoc
  result_t readFromFile(const string filename, string& errorDescription, bool verbose = false,
      map<string, string>* defaults = NULL, size_t* hash = NULL, size_t* size = NULL, time_t* time = NULL) override;

  // @copydoc
  result_t addFromFile(map<string, string>& row, vector< map<string, string> >& subRows,
      string& errorDescription, const string filename, unsigned int lineNo) override;

  /**
   * Get the scan @a Message instance for the specified address.
   * @param dstAddress the destination address, or @a SYN for the base scan @a Message.
   * @return the scan @a Message instance, or NULL if the dstAddress is no slave.
   */
  Message* getScanMessage(const symbol_t dstAddress = SYN);

  /**
   * Return whether additional scan @a Message instances are available.
   * @return whether additional scan @a Message instances are available.
   */
  bool hasAdditionalScanMessages() const { return m_additionalScanMessages; }

  /**
   * Resolve all @a Condition instances.
   * @param errorDescription a string in which to store the error description in case of error.
   * @param verbose whether to verbosely add all problems to the error message.
   * @return @a RESULT_OK on success, or an error code.
   */
  result_t resolveConditions(string& errorDescription, bool verbose = false);

  /**
   * Resolve a @a Condition.
   * @param condition the @a Condition to resolve.
   * @param errorDescription a string in which to store the error description in case of error.
   * @param readMessageFunc the function to call for immediate reading of a @a Message from the bus, or NULL.
   * @return @a RESULT_OK on success, or an error code.
   */
  result_t resolveCondition(Condition* condition, string& errorDescription,
      void (*readMessageFunc)(Message* message) = NULL);

  /**
   * Run all executable @a Instruction instances.
   * @param log the @a ostringstream to log success messages to (if necessary).
   * @param readMessageFunc the function to call for immediate reading of a
   * @a Message values from the bus required for singleton instructions, or NULL.
   * @return @a RESULT_OK on success, or an error code.
   */
  result_t executeInstructions(ostringstream& log, void (*readMessageFunc)(Message* message) = NULL);

  /**
   * Add a loaded file to a participant.
   * @param address the slave address.
   * @param filename the name of the configuration file (including relative path).
   * @param comment an optional comment.
   */
  void addLoadedFile(const symbol_t address, const string filename, const string comment = "");

  /**
   * Get the loaded files for a participant.
   * @param address the slave address.
   * @return the loaded configuration files (list of file names with relative path).
   */
  const vector<string>& getLoadedFiles(const symbol_t address) const;

  /**
   * Get all loaded files.
   * @return the loaded configuration files (list of file names with relative path).
   */
  vector<string> getLoadedFiles() const;

  /**
   * Get the infos for a loaded file.
   * @param filename the name of the configuration file (including relative path).
   * @param comment a string in which the comment is stored.
   * @param hash optional pointer to a @a size_t value for storing the hash of the file, or NULL.
   * @param size optional pointer to a @a size_t value for storing the normalized size of the file, or NULL.
   * @param time optional pointer to a @a time_t value for storing the modification time of the file, or NULL.
   * @return true if the file info was found, false otherwise.
   */
  bool getLoadedFileInfo(const string filename, string& comment, size_t* hash = NULL, size_t* size = NULL,
      time_t* time = NULL) const;

  /**
   * Get the stored @a Message instances for the key.
   * @param key the key of the @a Message.
   * @return the found @a Message instances, or NULL.
   * Note: the caller may not free the returned instances.
   */
  const vector<Message*>* getByKey(const uint64_t key) const;

  /**
   * Find the @a Message instance for the specified circuit and name.
   * @param circuit the optional circuit name.
   * @param name the message name.
   * @param levels the access levels to match.
   * @param isWrite whether this is a write message.
   * @param isPassive whether this is a passive message.
   * @return the @a Message instance, or NULL.
   * Note: the caller may not free the returned instance.
   */
  Message* find(const string& circuit, const string& name, const string& levels, const bool isWrite,
    const bool isPassive = false) const;

  /**
   * Find all active get @a Message instances for the specified circuit and name.
   * @param circuit the circuit name, or empty for any.
   * @param name the message name, or empty for any.
   * @param levels the access levels to match.
   * @param completeMatch false to also include messages where the circuit and name matches only a part of the given
   * circuit and name (default true).
   * @param withRead true to include read messages (default true).
   * @param withWrite true to include write messages (default false).
   * @param withPassive true to include passive messages (default false).
   * @return the found @a Message instances.
   * @param includeEmptyLevel true to also include messages with no access level, false to include only messages with
   * the specified level.
   * @param onlyAvailable true to include only available messages (default true), false to also include messages that
   * are currently not available (e.g. due to unresolved or false conditions).
   * @param since the start time from which to add updates (inclusive, also removes messages with unset destination
   * address), or 0 to ignore.
   * @param until the end time to which to add updates (exclusive, also removes messages with unset destination
   * address), or 0 to ignore.
   * Note: the caller may not free the returned instances.
   */
  deque<Message*> findAll(const string& circuit, const string& name, const string& levels,
    const bool completeMatch = true, const bool withRead = true, const bool withWrite = false,
    const bool withPassive = false, const bool includeEmptyLevel = true, const bool onlyAvailable = true,
    const time_t since = 0, const time_t until = 0) const;

  /**
   * Find the @a Message instance for the specified master data.
   * @param master the @a MasterSymbolString for identifying the @a Message.
   * @param anyDestination true to only return messages without a particular destination.
   * @param withRead true to include read messages (default true).
   * @param withWrite true to include write messages (default true).
   * @param withPassive true to include passive messages (default true).
   * @param onlyAvailable true to include only available messages (default true), false to also include messages that
   * are currently not available (e.g. due to unresolved or false conditions).
   * @return the @a Message instance, or NULL.
   * Note: the caller may not free the returned instance.
   */
  Message* find(const MasterSymbolString& master, const bool anyDestination = false, const bool withRead = true,
      const bool withWrite = true, const bool withPassive = true, const bool onlyAvailable = true) const;

  /**
   * Invalidate cached data of the @a Message and all other instances with a matching name key.
   * @param message the @a Message to invalidate.
   */
  void invalidateCache(Message* message);

  /**
   * Add a @a Message to the list of instances to poll.
   * @param message the @a Message to poll.
   * @param toFront whether to add the @a Message to the very front of the poll queue.
   */
  void addPollMessage(Message* message, bool toFront = false);

  /**
   * Decode circuit specific data.
   * @param circuit the name of the circuit.
   * @param output the @a ostringstream to append the decoded value(s) to.
   * @param outputFormat the @a OutputFormat options to use.
   * @return true if data was added, false otherwise.
   */
  bool decodeCircuit(const string circuit, ostringstream& output, OutputFormat outputFormat) const;

  /**
   * Removes all @a Message instances.
   */
  void clear();

  /**
   * Get the number of all stored @a Message instances.
   * @return the the number of all stored @a Message instances.
   */
  size_t size() const { return m_messageCount; }

  /**
   * Get the number of stored conditional @a Message instances.
   * @return the the number of stored conditional @a Message instances.
   */
  size_t sizeConditional() const { return m_conditionalMessageCount; }

  /**
   * Get the number of stored passive @a Message instances.
   * @return the the number of stored passive @a Message instances.
   */
  size_t sizePassive() const { return m_passiveMessageCount; }

  /**
   * Get the number of stored @a Message instances with a poll priority.
   * @return the the number of stored @a Message instances with a poll priority.
   */
  size_t sizePoll() const { return m_pollMessages.size(); }

  /**
   * Get the next @a Message to poll.
   * @return the next @a Message to poll, or NULL.
   * Note: the caller may not free the returned instance.
   */
  Message* getNextPoll();

  /**
   * Get the number of stored @a Condition instances.
   * @return the number of stored @a Condition instances.
   */
  size_t sizeConditions() const { return m_conditions.size(); }

  /**
   * Get the stored @a Condition instances.
   * @return the @a Condition instances by filename and condition name.
   */
  const map<string, Condition*>& getConditions() const { return m_conditions; }

  /**
   * Write the message definitions to the @a ostream.
   * @param output the @a ostream to append the formatted messages to.
   * @param withConditions whether to include the optional conditions prefix.
   */
  void dump(ostream& output, const bool withConditions = false) const;


 private:
  /** empty vector for @a getLoadedFiles(). */
  static vector<string> s_noFiles;

  /** the path to the configuration files. */
  const string m_configPath;

  /** whether to add all messages, even if duplicate. */
  const bool m_addAll;

  /** the @a Message instance used for scanning a slave. */
  Message* m_scanMessage;

  /** the @a Message instance used for sending the broadcast scan request. */
  Message* m_broadcastScanMessage;

  /** whether additional scan @a Message instances are available. */
  bool m_additionalScanMessages;

  /** the loaded configuration files by slave address (list of file names with relative path). */
  map<symbol_t, vector<string>> m_loadedFiles;

  /** the @a LoadedFileInfo by for load configuration files (by file name with relative path). */
  map<string, LoadedFileInfo> m_loadedFileInfos;

  /** the maximum ID length used by any of the known @a Message instances. */
  size_t m_maxIdLength;

  /** the maximum ID length used by any of the known @a Message instances with destination @a BROADCAST. */
  size_t m_maxBroadcastIdLength;

  /** the number of distinct @a Message instances stored in @a m_messagesByName. */
  size_t m_messageCount;

  /** the number of conditional @a Message instances part of @a m_messageCount. */
  size_t m_conditionalMessageCount;

  /** the number of distinct passive @a Message instances stored in @a m_messagesByKey. */
  size_t m_passiveMessageCount;

  /** the known @a Message instances by lowercase circuit (optional), name, and type. */
  map<string, vector<Message*> > m_messagesByName;

  /** the known @a Message instances by key. */
  map<uint64_t, vector<Message*> > m_messagesByKey;

  /** the known @a Message instances to poll, by priority. */
  MessagePriorityQueue m_pollMessages;

  /** the @a Condition instances by filename and condition name. */
  map<string, Condition*> m_conditions;

  /** the list of @a Instruction instances by filename. */
  map<string, vector<Instruction*> > m_instructions;

  /** additional attributes by circuit name. */
  map<string, AttributedItem*> m_circuitData;
};

}  // namespace ebusd

#endif  // LIB_EBUS_MESSAGE_H_
