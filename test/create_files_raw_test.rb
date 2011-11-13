require File.expand_path(File.join(File.dirname(__FILE__), 'test_helper'))
require File.expand_path(File.join(File.dirname(__FILE__), 'create_files_tests'))

class CreateFilesRawTest < Verm::TestCase
  def setup
    super
    @raw = true
  end

  include CreateFilesSharedTests
end
